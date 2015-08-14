#include "control.h"
#include "camera.h"
#include "PID.h"
#include <dfs_posix.h>
#include <finsh.h>
#include "pwm_remote.h"
extern pwm_signal_t pwm;

#define PID_SS 0xABCD
#define PID_ES 0xDCBA

#define PID_PATH "/lt.pid"

#define STRIGHT_MODE	0
#define CRUISE_MODE		4
#define THROW_MODE		3

#define BASIC_THROTTLE 	470
#define BASIC_HEIGHT 	50.0f
#define TAKEOFF_TIME 	RT_TICK_PER_SECOND*0.4f
#define LAND_TIME		RT_TICK_PER_SECOND/2
#define LINE_STOP		12
#define TURN_STOP		12.0f
#define TURN_PITCH		-3.0f
#define TURN_ROLL		-1.0f
#define GO_PITCH		-2.0f
#define V_EXPECT		-0.5f

fc_task * line_task;
fc_task * cruise_task;
fc_task * throw_task;

struct line_pid
{
	uint16_t SS;
	PID dist;
	PID angle;
	uint16_t ES;
}pid;

float cv=0;
float Iv(float ve)
{
	cv=ahrs.degree_pitch*ahrs.time_span*9.8f;
	PID_SetTarget(&pid.angle,ve);
	PID_xUpdate(&pid.angle,cv);
	return -RangeValue(pid.angle.out,-10,10);
}

rt_err_t line_track(u8 var)
{
	static float h;
	static float yaw;
	static uint8_t waitl;
	static int8_t stop;
	static float left;
	static int i;
	static float target;
	static int turn;
	static int time;
	
	tPre;
	if(current_task->reset)
	{
		tReset;
		current_task->reset=RT_FALSE;
	}
	tBegin;
	PID_Reset(&pid.dist);
	PID_Reset(&pid.angle);
	yaw=ahrs.degree_yaw;
	h=0;
	stop=0;
	left=0;
	turn=0;
	cv=0;
//takeoff:
	rt_kprintf("takeoff.\n");
	GPIO_WriteBit(GPIOE,GPIO_Pin_1,Bit_RESET);
	GPIO_WriteBit(GPIOE,GPIO_Pin_2,Bit_SET);
	while(h<49.0f)//take off
	{
		h=linear(h,5.0f,70,RT_TICK_PER_SECOND*0.4f);
		stable(Iv(V_EXPECT),0,yaw);
		
		althold(50.0f);
		
		motor_hupdate(400+(u16)h);
		
//		rt_kprintf("%d/%d\n",(u8)ahrs.height,(u8)h);
		
		tReturn(RT_EOK);
	}
	rt_uint32_t dump;
line: 
	waitl=0;
	while(1) //line track 
	{
		if (rt_event_recv(&ahrs_event, AHRS_EVENT_CARMERA, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, RT_WAITING_NO, &dump) == RT_EOK)
		{
			rt_kprintf("e:%d\to:%d\th:%d\ta:%d\ts:%d\n",(s16)ahrs.line_err,(s16)pid.dist.out,(s16)ahrs.height,(s16)recv.pack.angle_error,(s16)recv.pack.linestate);
			
			switch(recv.pack.linestate)
			{
				case LINE_MARK:
					if(turn>=var&&var!=STRIGHT_MODE)
					{
						stop=12;
						goto land;
					}
				case LINE_STRAIGHT:
					PID_SetTarget(&pid.dist,0);
					PID_xUpdate(&pid.dist,ahrs.line_err);
					left*=0.5f;
					waitl*=0.8f;
				
//					if(turn>=4&&GPIO_ReadInputDataBit(GPIOE,GPIO_Pin_0)&&abs(recv.pack.middle_error)<60)
//					{
//						stop=-2;
//						goto land;
//					}
				break;
				case LINE_END:
					stop=12;
					if(waitl>=2.0f)
					{
						if(var==THROW_MODE)
						{
							GPIO_WriteBit(GPIOE,GPIO_Pin_2,Bit_RESET);
							GPIO_WriteBit(GPIOE,GPIO_Pin_1,Bit_SET);
							goto turnl;
						}
						goto land;
					}
				case LINE_LOST_ERROR:
					if(abs(recv.pack.middle_error)>100)
						waitl+=0.5f;
					else
						waitl+=1.0f;
					if(waitl>=20.0f)
						goto land;
					break;
				case LINE_TURN_LEFT_90:
					if(turn>=var&&var!=STRIGHT_MODE)
					{
						stop=12;
						goto land;
					}
					if(var!=STRIGHT_MODE)
					{
						left+=1.0f;
						if(left>3.0f)
						{
							goto turnl;
						}
					}
					break;
				default:
					left*=0.9f;
				break;
			}
		}
//		float y=rangeYaw( yaw+ahrs.angle_err);
		stable(Iv(V_EXPECT),RangeValue(pid.dist.out,-10,10),yaw);
		althold(45);
		motor_hupdate(BASIC_THROTTLE);
		
		tReturn(RT_EOK);
	}
turnl:
	left=0;
	turn++;
	rt_kprintf("stop at %d.\n",(u8)ahrs.height);
	if(ahrs.height>38)
		time=50;
	else
		time=80;
	for(i=0;i<time;i++)
	{
		stable(Iv(0),0,yaw);
		althold(55);
		motor_hupdate(BASIC_THROTTLE);
		tReturn(RT_EOK);
	}
	target=rangeYaw(yaw-90.0f);
	rt_kprintf("turn left to %d.\n",(s16)target);
	while(1)
	{
		float diff;
		diff=diffYaw(ahrs.degree_yaw,target);
		if((diff<10.0f&&diff>-10.0f)||(diff<30.0f&&diff>-30.0f&&recv.pack.linestate==LINE_STRAIGHT))
		{
			yaw=target;
			cv=0;
			goto line;
		}
		stable(Iv(0),RangeValue(pid.dist.out,-4,4)+TURN_ROLL,target);
		althold(60);
		motor_hupdate(BASIC_THROTTLE);
		tReturn(RT_EOK);
	}
land:
	rt_kprintf("land %d.\n",(s16)stop);
	h=BASIC_THROTTLE;
	while(h>60.0f&&ahrs.height>20.0f)//land
	{
		h=linear(h,BASIC_THROTTLE,0,RT_TICK_PER_SECOND/2);
		stable(stop,0,yaw);
		
		motor_update((u16)h);
		
		tReturn(RT_EOK);
	}
	GPIO_WriteBit(GPIOE,GPIO_Pin_2,Bit_RESET);
	GPIO_WriteBit(GPIOE,GPIO_Pin_1,Bit_RESET);
	tReturn(1);
	tFinish
	return 1;
}

rt_err_t test_task(u8 var)
{
	static float h;
	static float yaw;
	static uint8_t stop;
	static float target;
	
	tPre;
	if(current_task->reset)
	{
		tReset;
		current_task->reset=RT_FALSE;
	}
	tBegin;
	PID_Reset(&pid.dist);
	yaw=ahrs.degree_yaw;
	h=0;
	stop=0;
//takeoff:
	rt_kprintf("takeoff.\n");
	while(h<49.0f)//take off
	{
		h=linear(h,5.0f,50.0f,RT_TICK_PER_SECOND/2);
		stable(0,0,yaw);
		
		althold(50.0f);
		
		motor_hupdate(400+(u16)h);
		
		tReturn(RT_EOK);
	}
turnl:
	target=rangeYaw(yaw-90.0f);
	rt_kprintf("turn left to %d.\n",(s16)target);
	while(1)
	{
		float diff;
//		deg=linear(deg,0,15.0f,RT_TICK_PER_SECOND);
//		yaw+=pwm.yaw*0.5f;//rangeYaw(target+15.0f-deg);
//		yaw=rangeYaw(yaw);
//		rt_kprintf("y:%d/%d\n",(s16)ahrs.degree_yaw,(s16)yaw);
		diff=diffYaw(ahrs.degree_yaw,target);
		if(diff<5.0f&&diff>-5.0f)
		{
			yaw=target;
			goto land;
		}
		stable(0.0f,0.0f,target);
		althold(50);
		motor_hupdate(450);
		tReturn(RT_EOK);
	}
land:
	rt_kprintf("land.\n");
	h=450;
	while(h>60.0f&&ahrs.height>10.0f)//land
	{
		h=linear(h,450,50,RT_TICK_PER_SECOND/2);
		if(ahrs.height>20.0f)
			stable(stop,0,yaw);
		else
			stable(0,0,yaw);
		
		motor_update((u16)h);
		
		tReturn(RT_EOK);
	}
	tReturn(1);
	tFinish
	return RT_EOK;
}

static void init_pid()
{
	int fd;
	fd = open(PID_PATH, O_RDWR | O_CREAT, 0);

	if (fd >= 0)
	{
		if (read(fd, &pid, sizeof(pid)) != sizeof(pid) ||
			pid.SS != PID_SS || pid.ES != PID_ES)
		{
			rt_kprintf("init line track pid.\n");
			pid.SS = PID_SS;
			pid.ES = PID_ES;
			
			PID_Init(&pid.angle,0,0,0);
			PID_Init(&pid.dist,0,0,0);
			
			PID_Set_Filt_Alpha(&pid.angle,1.0f/166.0f,20.0);
			PID_Set_Filt_Alpha(&pid.dist,1.0f/60.0f,20.0);

			write(fd, &pid, sizeof(pid));
		}
		else
		{
			PID_Reset(&pid.angle);
			PID_Reset(&pid.dist);
			PID_Set_Filt_Alpha(&pid.angle,1.0f/166.0f,20.0);
			PID_Set_Filt_Alpha(&pid.dist,1.0f/60.0f,20.0);
			rt_kprintf("line track pid load succeed.\n");
		}
		close(fd);
	}
	else
	{
		rt_kprintf("line track open wrong.\n");
	}
	rt_kprintf("line angle:		%d.%03d	%d.%02d	%d.%03d.\n", (s32)pid.angle.p, (s32)(pid.angle.p*1000.0f) % 1000,
	(s32)pid.angle.i, (s32)(pid.angle.i*100.0f) % 100,
	(s32)pid.angle.d, (s32)(pid.angle.d*1000.0f) % 1000);
	rt_kprintf("line dist :		%d.%03d	%d.%02d	%d.%03d.\n", (s32)pid.dist.p, (s32)(pid.dist.p*1000.0f) % 1000,
	(s32)pid.dist.i, (s32)(pid.dist.i*100.0f) % 100,
	(s32)pid.dist.d, (s32)(pid.dist.d*1000.0f) % 1000);
}

static void save_pid()
{
	int fd;

	fd = open(PID_PATH, O_WRONLY | O_TRUNC, 0);

	if (fd >= 0)
	{
		write(fd, &pid, sizeof(pid));
		close(fd);
	}
	rt_kprintf("line angle:		%d.%03d	%d.%02d	%d.%03d.\n", (s32)pid.angle.p, (s32)(pid.angle.p*1000.0f) % 1000,
	(s32)pid.angle.i, (s32)(pid.angle.i*100.0f) % 100,
	(s32)pid.angle.d, (s32)(pid.angle.d*1000.0f) % 1000);
	rt_kprintf("line dist :		%d.%03d	%d.%02d	%d.%03d.\n", (s32)pid.dist.p, (s32)(pid.dist.p*1000.0f) % 1000,
	(s32)pid.dist.i, (s32)(pid.dist.i*100.0f) % 100,
	(s32)pid.dist.d, (s32)(pid.dist.d*1000.0f) % 1000);
}

void set_la(s16 p, s16 i, s16 d)
{
	PID_Init(&pid.angle, p / 1000.0f, i / 100.0f, d / 1000.0f);
	save_pid();
}
FINSH_FUNCTION_EXPORT(set_la, set the value of pid in line track angle)

void set_ld(s16 p, s16 i, s16 d)
{
	PID_Init(&pid.dist, p / 1000.0f, i / 100.0f, d / 1000.0f);
	save_pid();
}
FINSH_FUNCTION_EXPORT(set_ld, set the value of pid in line track dist)

void line_register()
{
	fc_task * tmp;
	GPIO_InitTypeDef gpio_init;
	
	GPIO_StructInit(&gpio_init);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE,ENABLE);
	gpio_init.GPIO_Mode=GPIO_Mode_IN;
	gpio_init.GPIO_Pin=GPIO_Pin_0;
	gpio_init.GPIO_PuPd=GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOE,&gpio_init);
	gpio_init.GPIO_Mode=GPIO_Mode_OUT;
	gpio_init.GPIO_Pin=GPIO_Pin_1|GPIO_Pin_2;
	GPIO_Init(GPIOE,&gpio_init);
	GPIO_WriteBit(GPIOE,GPIO_Pin_2,Bit_RESET);
	GPIO_WriteBit(GPIOE,GPIO_Pin_1,Bit_RESET);
	
	
	line_task=find_task("default");
	cruise_task=find_task("cruise");
	throw_task =find_task("throw");
	assert_param(line_task!=RT_NULL);
	assert_param(cruise_task!=RT_NULL);
	assert_param(throw_task!=RT_NULL);
	tmp=find_task("test");
	
	line_task->func=line_track;
	cruise_task->func=line_track;
	throw_task->func=line_track;
	tmp->func=test_task;
	
	line_task->var=STRIGHT_MODE;
	cruise_task->var=CRUISE_MODE;
	throw_task->var=THROW_MODE;
	
	init_pid();
}
