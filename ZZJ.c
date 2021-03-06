//==============================================================================
//
// Title:		ZZJ
// Purpose:		A short description of the application.
//
// Created on:	17/03/17 周五 at 9:51:15 by shuaiqi.
// Copyright:	. All Rights Reserved.
//
//==============================================================================

//==============================================================================
// Include files
#include <ansi_c.h>
#include <windows.h>
#include <analysis.h>
#include <cvirte.h>
#include <userint.h>
#include "ZZJ.h"
#include "Portable.h"
#include "toolbox.h"
#include "log.h"
#include "TimeMeasure.h"
#include "UserThread.h"
#include "UserThreadQueue.h"
#include "global.h"

#if defined(PORTABLE) 
	#include <NIDAQmx.h> 
#else
	typedef void*              TaskHandle;
	typedef signed long        int32;
	typedef double             float64;
#endif

#define SIMULATE_INPUT  //  软件模拟输入数据，注释掉本行则使用采集卡

//==============================================================================
// Constants

//==============================================================================
// Types
typedef enum
{
	WORK_MODEL_MANUAL,
	WORK_MODEL_AUTO,
} WORK_MODEL;

typedef enum
{
	RUN_MODEL_STOP,
	RUN_MODEL_RUNNING,
	RUN_MODEL_QUIT,
} RUN_MODEL;

typedef struct
{

	TaskHandle	task;
	int32		numSampsPerChan;
	int			startCtrl;
	int			stopCtrl;
	int         chartPanel;
	// 绘图控件和参数
	int			chartCtrl;
	int 		scalingMode[2];
	double 		min[2],max[2];
	// 定时器
	int			timerCtrl;
	RUN_MODEL   running;
	float64		timeout;
	DWORD		startTick;     // 测量开始时候的tick
	DWORD       processedTick; // 已经处理过的Tick
	WORK_MODEL  workModel;     // 测量模式：自动测量，手动测量
} StartReadWfmLoopStopInfo;
StartReadWfmLoopStopInfo g_info;




typedef enum
{
	CHANNEL_I,   		// 电流通道
	CHANNEL_V,   		// 电压通道
	CHANNEL_VREF,		// 参考通道
	CHANNEL_FORCE		// 力通道
} CHANNEL;

typedef enum
{
	CHANNEL_TYPE_ALL_DATA,   // 通道内所有数据
	CHANNEL_TYPE_LAST_DATA   // 通道内最后添加的数据
} CHANNEL_TYPE;



char g_FileName[MAX_PATHNAME_LEN];// 波形文件名
// 设置波形参数
typedef struct
{
	double *addr;
	double **lastAddr;
	int *len;
	int *lastLen;
	double *increment;

} ChannelDataPoint;


/*
SWITCH_MODEL // 转辙机型号
FORCE_SENSOR_TYPE // 力传感器类型
SWITCH_NUM //转辙机编号
TEST_ADDR  // 测试地点
TEST_PERSON // 测试人
TEST_TIME  // 测试日期
*/
typedef struct
{
	char switchModel[50]; 		// 转辙机型号
	char forceSensorType[50]; 	// 力传感器类型
	char switchNum[50]; 		// 转辙机编号
	char TestAddr[50]; 			// 测试地点
	char TestPerson[50]; 		// 测试人
	time_t TestTime; 			// 测试日期
} UserData;
UserData g_UserData;



//==============================================================================
// Static global variables

static int panelMain = 0;   // 主界面:启动后显示
static int panelMeasure = 0;// 配置界面:开始测量参数
static int panelSystem = 0; // 系统界面
static int panelGraph  = 0; // 波形图界面
static int panelPrint = 0;  // 打印界面
static int panelUserData = 0; 		// 用户输入数据界面
static int panelUserDataCmd = 0;	// 用户输入数据命令界面

static double initialX;     // 波形图电流、电压初始x的位置。
static int initialVRef;     // 波形图"电压基准"初始位置
static int initialForce;    // 波形图力的初始位置



//==============================================================================
// Static functions
#define DAQmxErrChk(functionCall) if( (DAQmxError=(functionCall))<0 ) goto Error; else
#define DAQmxNullChk(functionCall) if( (functionCall)==NULL ) { DAQmxError=DAQmxErrorPALMemoryFull; goto Error; } else
#define DAQmxReportErr(statusExpression) \
	{ \
		int32 __DAQmxError = (statusExpression); \
		if (DAQmxFailed(__DAQmxError) && !GetBreakOnLibraryErrors()) \
		{ \
			char gErrBuf[512]; \
			DAQmxGetErrorString(__DAQmxError,gErrBuf,512); \
			MessagePopup("DAQmx Error",gErrBuf); \
		} \
	}
//==============================================================================
// Global variables
Waveform g_Waveform;
//==============================================================================
// Global functions
int CVICALLBACK ThreadAcquire(void *functionData);
ThreadControl g_ThreadControls[]={{ThreadAcquire}};// 线程定义，第一项为线程的回掉函数，其他项均为0

int Acquisition(BOOL isStart);
void Measure(int isStart);
int DisplayCursorData(int panel,int control);  // 显示光标处的力，电流等参数
int EnableCursor(int panel, int control) ; 	   // 显示/隐藏光标
void PlotData(CHANNEL_TYPE chanType,int panel,int graphCtrl);  // 电流:红色，电压：绿色，电压基准：蓝色，转换力：白色
int UpdateUserData(int panel,BOOL isSave);	// panel：需要更新的面板。TRUE:界面到内存数据，FALSE:内存数据到界面
int TBDisplayTime( void );                  // 界面显示当前时间
int LoadALLData();

BOOL IsDCSwitchMachine()
{
	if(!strcmp(g_UserData.switchModel,"S700K-C"))
	{
		return FALSE;
	}
	else
	{
		return TRUE; 
	}
}			

int WaveformClr(CHANNEL channel);
#if 0
int CreatWaveformName(char *fileName,)
{
	char strTime[50];
	time_t testTime = g_UserData.TestTime;
	strftime(strTime,sizeof(strTime),"%Y年%m月%d日 %H时%M分%S秒",localtime(&testTime));
	sprintf(fileName,"%s-%s-%s.vif",g_UserData.switchModel,g_UserData.switchNum,strTime);
	return 0;
}
#else
int CreatWaveformName(char *fileName,char *switchNum)
{
	char strTime[50];
	time_t testTime = g_UserData.TestTime;
	strftime(strTime,sizeof(strTime),"%Y年%m月%d日 %H时%M分%S秒",localtime(&testTime));
	sprintf(fileName,"%s-%s-%s.vif",g_UserData.switchModel,switchNum,strTime);
	return 0;
}
#endif


void WaveformInit(void)
{
	WaveformClr(CHANNEL_I);		// 电流通道
	WaveformClr(CHANNEL_V);		// 电压通道
	WaveformClr(CHANNEL_VREF);  // 参考通道
	WaveformClr(CHANNEL_FORCE); // 力通道

	g_Waveform.rawDataLen = 0;  // 测量数据的长度归零。
	g_ConvertedLen = 0;         // rawData已经转换过的长度
	g_Waveform.IIncrement = 0.02;     // 1/50Hz= 0.02s
	g_Waveform.VIncrement = 0.02;
	g_Waveform.VRefIncrement = 1.0/SAMPLE_RATE;
	g_Waveform.forceIncrement= 1.0/100;  // 每秒100次为力传感器变化的频率上限
	initialX = 0;
	initialVRef = 0;
	initialForce = 0;
}
static int WaveformChannel2Addr(CHANNEL channel,ChannelDataPoint* pData)
{
	int ret=0;
	switch(channel)
	{
		case CHANNEL_I:
			pData->addr = g_Waveform.I;
			pData->lastAddr = &g_Waveform.ILastAddr;
			pData->len  = &g_Waveform.ILen;
			pData->lastLen = &g_Waveform.ILastLen;
			pData->increment = &g_Waveform.IIncrement;
			break;
		case CHANNEL_V:
			pData->addr = g_Waveform.V;
			pData->lastAddr = &g_Waveform.VLastAddr;
			pData->len  = &g_Waveform.VLen;
			pData->lastLen = &g_Waveform.VLastLen;
			pData->increment = &g_Waveform.VIncrement;
			break;
		case CHANNEL_VREF:
			pData->addr = g_Waveform.VRef;
			pData->lastAddr = &g_Waveform.VRefLastAddr;
			pData->len  = &g_Waveform.VRefLen;
			pData->lastLen = &g_Waveform.VRefLastLen;
			pData->increment = &g_Waveform.VRefIncrement;
			break;
		case CHANNEL_FORCE:
			pData->addr = g_Waveform.force;
			pData->lastAddr = &g_Waveform.forceLastAddr;
			pData->len  = &g_Waveform.forceLen;
			pData->lastLen = &g_Waveform.forceLastLen;
			pData->increment = &g_Waveform.forceIncrement;
			break;
#if 0			
		default:
			ret = -1;
			ERR1("Channel2Addr 参数错误。channel:%d",channel);
			goto Error;
#endif			
			//break;
	}
Error:
	return ret;
}
int WaveformClr(CHANNEL channel)
{
	int ret=0;
	ChannelDataPoint chData;
	if(WaveformChannel2Addr(channel,&chData)<0)
	{
		ret = -1;
		ERR1("WaveformSet 参数错误。channel:%d",channel);
		goto Error;
	}
	*chData.len = 0;
	*chData.increment = 0;
	*chData.lastAddr = 0;
	*chData.lastLen = 0;	
Error:
	return ret;
}


int WaveformSet(CHANNEL channel,double *data,int len,int x)// channel.
{
	int ret=0;
	ChannelDataPoint chData;
	if(WaveformChannel2Addr(channel,&chData)<0)
	{
		ret = -1;
		ERR1("WaveformSet 参数错误。channel:%d",channel);
		goto Error;
	}
	memcpy(chData.addr+x,data,len*sizeof(double));
	*chData.len = len + x;
	*chData.lastLen = len;
	*chData.lastAddr = chData.addr+x;
	//*chData.increment = increment;

Error:
	return ret;
}
/*
输出：
	data：波形地址
	len	:波形长度
	x：波形起始点(时间)x坐标
	increment:波形每两个点之间的间隔时间
*/
int WaveformGet(CHANNEL channel,CHANNEL_TYPE channelType,double **data,int* len,double* x,double* increment)
{
	int ret=0;
	double xStart=0; // 波形起始点/最后更新开始点(时间)x坐标
	ChannelDataPoint chData;

	if(WaveformChannel2Addr(channel,&chData)<0)
	{
		ret = -1;
		ERR1("WaveformGet 参数错误。channel:%d",channel);
		goto Error;
	}
	switch(channelType)
	{
		case CHANNEL_TYPE_ALL_DATA:
			*data = chData.addr;
			*len  = *chData.len;
			break;
		case CHANNEL_TYPE_LAST_DATA:
			*data = *chData.lastAddr;
			*len  = *chData.lastLen;
			if((*len - *chData.len)!=0)
			{
				*data -=1;
				*len +=1;
				xStart = (*chData.lastAddr - chData.addr -1)*(*chData.increment);
			}
			else
			{
				xStart = 0;
			}
			break;
#if 0			
		default:
			ERR1("WaveformGet 参数错误，channelType：%d",channelType);
			goto Error;
			break;
#endif			
	}
	//*x = (*chData.lastAddr - chData.addr)*(*chData.increment);
	if(increment!=NULL)
		*increment = *chData.increment;
	if(x != NULL)
		*x = xStart;
Error:
	return ret;
}
/////////////////////////////////////////////////////////
// UI用户界面
#if 0  // 以最大最小值重设坐标轴
int UpdatePlot()
{
	// 重绘制，依照采集到的最大值重设纵坐标轴。
	SetCtrlAttribute(g_info.chartPanel, g_info.chartCtrl,ATTR_REFRESH_GRAPH,0);
	DeleteGraphPlot(panelGraph,GRAPH_GRAPH,-1,VAL_DELAYED_DRAW); // 清空绘图区
	//SetCtrlAttribute(g_info.chartPanel,g_info.chartCtrl,ATTR_ENABLE_ANTI_ALIASING,TRUE);
	SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_LEFT_YAXIS,VAL_AUTOSCALE,0,0);
	SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_RIGHT_YAXIS,VAL_AUTOSCALE,0,0);

	PlotData(CHANNEL_TYPE_ALL_DATA,g_info.chartPanel, g_info.chartCtrl);

	//SetCtrlAttribute(g_info.chartPanel,g_info.chartCtrl,ATTR_ENABLE_ANTI_ALIASING,FALSE);
	SetCtrlAttribute(g_info.chartPanel, g_info.chartCtrl,ATTR_REFRESH_GRAPH,1);
	return 0;
}
#else // 以最大值重设坐标轴
#if 0
int SetWaveformAxies(CHANNEL channel,int axis)
{
	double* waveform;
	double max,min,maxABS;
	int len,imax,imin;	
	WaveformGet(channel,CHANNEL_TYPE_ALL_DATA,&waveform,&len,NULL,NULL);	// 力波形
	if(len>0){
		MaxMin1D(waveform,len,&max,&imax,&min,&imin);
		max = fabs(max);
		min = fabs(min);
		maxABS = max>min?max:min; // 取绝对值的最大值。
		maxABS = (int)maxABS +1;

		SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,axis,VAL_MANUAL,-maxABS,maxABS);
	}
	return 0;
}
#else
int GetWavefromAxiesMax(CHANNEL channel,int *axiesMax)
{
	int isErr = 0;
	double* waveform;
	double max,min,maxABS;
	int len,imax,imin;	
	WaveformGet(channel,CHANNEL_TYPE_ALL_DATA,&waveform,&len,NULL,NULL);	// 力波形
	if(len>0){
		MaxMin1D(waveform,len,&max,&imax,&min,&imin);
		max = fabs(max);
		min = fabs(min);
		maxABS = max>min?max:min; // 取绝对值的最大值。
		maxABS = (int)maxABS +1;
		*axiesMax = maxABS;
	}else{
		isErr = -1;
	}

	return isErr;
}

int CalculateWaveformAxies(CHANNEL channel1,CHANNEL channel2,double *Ch1AxiesMax,double *Ch2AxiesMax)
{
	int errno1 = 0;
	int axies[2];
	//int max,min;
	if((errno1 = GetWavefromAxiesMax(channel1,&axies[0]))!= 0)goto Error;
	if((errno1 = GetWavefromAxiesMax(channel2,&axies[1]))!= 0)goto Error;
#if 1
	double val;
	if(axies[0] > axies[1]){
		for(;;axies[1] ++)
		{
			val = axies[0]/axies[1];
			if(val == 1 || val == 2 || val == 5)
				break;
		}// 整倍数缩放
	}else{
		for(;;axies[0] ++)
		{
			val = axies[1]/axies[0];
			if(val == 1 || val == 2 || val == 5)
				break;
		}// 整倍数缩放
	}
#endif	
	
	*Ch1AxiesMax = axies[0];
	*Ch2AxiesMax = axies[1];
Error:
	return errno1;
}

void SetWaveformAxies()
{
	double Ch1AxiesMax,Ch2AxiesMax;
	if(CalculateWaveformAxies(CHANNEL_FORCE,CHANNEL_I,&Ch1AxiesMax,&Ch2AxiesMax)==0)
	{
		SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_LEFT_YAXIS,VAL_MANUAL,-Ch1AxiesMax,Ch1AxiesMax);
		SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_RIGHT_YAXIS,VAL_MANUAL,-Ch2AxiesMax,Ch2AxiesMax);
	}
}
#endif

int UpdatePlot()
{
	// 重绘制，依照采集到的最大值重设纵坐标轴。
	SetCtrlAttribute(g_info.chartPanel, g_info.chartCtrl,ATTR_REFRESH_GRAPH,0); // 更新坐标轴参数期间，禁止画面更新
	DeleteGraphPlot(panelGraph,GRAPH_GRAPH,-1,VAL_DELAYED_DRAW); // 清空绘图区
	//SetCtrlAttribute(g_info.chartPanel,g_info.chartCtrl,ATTR_ENABLE_ANTI_ALIASING,TRUE); // 打开抗锯齿

	//SetWaveformAxies(CHANNEL_FORCE,VAL_LEFT_YAXIS);
	//SetWaveformAxies(CHANNEL_I,VAL_RIGHT_YAXIS);
	SetWaveformAxies();

	PlotData(CHANNEL_TYPE_ALL_DATA,g_info.chartPanel, g_info.chartCtrl);

	//SetCtrlAttribute(g_info.chartPanel,g_info.chartCtrl,ATTR_ENABLE_ANTI_ALIASING,FALSE);
	SetCtrlAttribute(g_info.chartPanel, g_info.chartCtrl,ATTR_REFRESH_GRAPH,1); // 更新画面
	return 0;
}
#endif

void MeterHide(BOOL isHide)
{
	int panel = panelMain;
	
	int meterID[] = {MAIN_NUM_V, MAIN_UNIT_V,MAIN_NUM_I ,MAIN_UNIT_I,MAIN_NUM_FORCE, MAIN_UNIT_FORCE,
					 MAIN_NUM_VREF, MAIN_UNIT_VREF};

	for(int i=0;i<sizeof(meterID)/sizeof(int);++i)
	{
		SetCtrlAttribute (panel, meterID[i], ATTR_VISIBLE, !isHide);
	}
}

int SetUIChange(int isStart)
{
	if(isStart)   // 开始采集
	{
		// Example Core: Start the task.
		//DAQmxErrChk(DAQmxStartTask(g_info.task));
		//SetCtrlAttribute (panelMain, g_info.timerCtrl, ATTR_ENABLED, 1);
		DeleteGraphPlot(panelGraph,GRAPH_GRAPH,-1,VAL_DELAYED_DRAW);
		SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_LEFT_YAXIS, g_info.scalingMode[0],g_info.min[0],g_info.max[0]);
		SetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_RIGHT_YAXIS,g_info.scalingMode[1],g_info.min[1],g_info.max[1]);
		
		SetCtrlVal(panelMain,MAIN_LED_RUNING,TRUE);
		SetCtrlAttribute(panelMain,MAIN_PIC_MEASURE,ATTR_LABEL_TEXT ,"停止采集");
		SetCtrlAttribute(panelMain,MAIN_PIC_SAVE,   ATTR_VISIBLE,FALSE);
		SetCtrlAttribute(panelMain,MAIN_LED_SAVE,   ATTR_VISIBLE,FALSE);
		SetCtrlAttribute(panelMain,MAIN_PIC_OPEN,   ATTR_VISIBLE,FALSE);
		SetCtrlAttribute(panelMain,MAIN_LED_OPEN,   ATTR_VISIBLE,FALSE);
		//SetCtrlAttribute(panelMain,MAIN_PIC_PRINT,  ATTR_VISIBLE,FALSE);
		//SetCtrlAttribute(panelMain,MAIN_LED_PRINT,  ATTR_VISIBLE,FALSE);


		//SetCtrlAttribute(panel,control,ATTR_PICT_BGCOLOR,0x185776);
		MeterHide(FALSE);
		WaveformInit();
		UpdateUserData(panelMeasure,TRUE);

	}
	else  // 停止采集
	{

		//SetCtrlAttribute (panelMain, g_info.timerCtrl, ATTR_ENABLED, 0);
		SetCtrlVal(panelMain,MAIN_LED_RUNING,FALSE);
		SetCtrlAttribute(panelMain,MAIN_PIC_MEASURE,ATTR_LABEL_TEXT ,"开始采集");
		// 关闭面板，停止保存
		SetCtrlAttribute(panelMain,MAIN_PIC_SAVE,   ATTR_VISIBLE,TRUE);
		SetCtrlAttribute(panelMain,MAIN_LED_SAVE,   ATTR_VISIBLE,TRUE);
		SetCtrlAttribute(panelMain,MAIN_PIC_OPEN,   ATTR_VISIBLE,TRUE);
		SetCtrlAttribute(panelMain,MAIN_LED_OPEN,   ATTR_VISIBLE,TRUE);
		//SetCtrlAttribute(panelMain,MAIN_PIC_PRINT,  ATTR_VISIBLE,TRUE);
		//SetCtrlAttribute(panelMain,MAIN_LED_PRINT,  ATTR_VISIBLE,TRUE);

		MeterHide(TRUE);
		UpdatePlot(); // 重绘制，依照采集到的最大值重设纵坐标轴。
		// 更新转辙机参数面板
		UpdateUserData(panelMeasure,FALSE);
	}
	return 0;
}
/////////////////////////////////////////////////////////
// 状态机
typedef enum{
	FSM_STATE_IDEL,
	FSM_STATE_AUTO_MEASURE_1,	
	FSM_STATE_AUTO_MEASURE_2,			// 自动测量
	FSM_STATE_MANUAL_MEASURE,	// 手动测量
	FSM_STATE_QUIT
}FSM_STATE;

// 信号
typedef enum{
	FSM_SIG_UI_MEASURE,// 用户点击采集按钮	
	FSM_SIG_AUTO_MEASURE,
	FSM_SIG_START_MEASURE,
	FSM_SIG_STOP_MEASURE,
	FSM_SIG_SAVE,
	FSM_SIG_LOAD,
	FSM_SIG_PRINT,
	FSM_SIG_QUIT,
	FSM_SIG_LEN
}FSM_SIG_ID;

typedef struct{
	FSM_SIG_ID id;
	char describe[50];
}FSM_SIG;

typedef struct{
	volatile FSM_STATE	state;
}FSM_ID;


FSM_ID g_fsmID;
FSM_SIG g_fsmSig[] = {
	{FSM_SIG_UI_MEASURE,"用户点击采集按钮"},
	{FSM_SIG_AUTO_MEASURE,	"自动测量"},
	{FSM_SIG_START_MEASURE, "开始测量"},
	{FSM_SIG_STOP_MEASURE,	"停止测量"},
	{FSM_SIG_SAVE,			"保存"},
	{FSM_SIG_LOAD,			"读取"},
	{FSM_SIG_PRINT,			"打印"},
	{FSM_SIG_QUIT,			"退出"},
};
#define SetState(newState) fsmId->state = newState;

// SendFSMSig(g_fsmID,FSM_SIG_AUTO_MEASURE);
int GetFSMState(FSM_ID *fsmId,FSM_ID *fsm)
{
	*fsm = *fsmId;
	return 0;
}


void OnFSMStateQuitInFun()
{
	Measure(FALSE); 
	Acquisition(FALSE);	// 停止采集
	QuitUserInterface (0);
}


void OnFSMSig(FSM_ID *fsmId,FSM_SIG *sig)
{
	//FSM_ID fsm;
	//GetFSMState(fsmId,&fsm);
	switch(fsmId->state){
		case FSM_STATE_IDEL:
			if(sig->id == FSM_SIG_UI_MEASURE){
				DisplayPanel(panelMeasure);	//用户界面切换到采集设置面板
			}else if(sig->id == FSM_SIG_AUTO_MEASURE){  // 自动测量
				SetUIChange(1);
				Acquisition(TRUE);
				SetCtrlAttribute(panelMain,MAIN_TIMER_MEASURE,ATTR_ENABLED,TRUE);
				SetState(FSM_STATE_AUTO_MEASURE_1);				
			}else if(sig->id == FSM_SIG_START_MEASURE){ // 手动测量
				SetUIChange(1);
				Acquisition(TRUE);
				Measure(TRUE);
				SetState(FSM_STATE_MANUAL_MEASURE);				
			}else if(sig->id == FSM_SIG_SAVE){  		// 保存
				UpdateUserData(panelUserData,FALSE);
				DisplayPanel(panelUserData);			// 显示用户数据面板
				DisplayPanel(panelUserDataCmd);			// 显示用户数据命令面板
			}else if(sig->id == FSM_SIG_LOAD){  		// 查询
				LoadALLData();
			}else if(sig->id == FSM_SIG_PRINT){  		// 
			}else if(sig->id == FSM_SIG_QUIT){  		// 退出
				OnFSMStateQuitInFun();				
				SetState(FSM_STATE_QUIT);
			}			
			break;
		case FSM_STATE_MANUAL_MEASURE:
		case FSM_STATE_AUTO_MEASURE_2:	// 自动测量中		
			if(sig->id == FSM_SIG_UI_MEASURE){			// 用户停止测量
				Measure(FALSE);				
				Acquisition(FALSE);
				SetUIChange(0);	
				//SaveTimeMeasure("OnTime.txt");
				SaveTimeMeasure("OnTime.txt");
				SetState(FSM_STATE_IDEL);
			}else if(sig->id == FSM_SIG_STOP_MEASURE){	// 测试时间到，停止
				Measure(FALSE);
				Acquisition(FALSE);
				SetUIChange(0);					
				//SaveTimeMeasure("OnTime.txt");
				SaveTimeMeasure("OnTime.txt");
				SetState(FSM_STATE_IDEL);
			}else if(sig->id == FSM_SIG_QUIT){			// 退出程序
				OnFSMStateQuitInFun();
				SetState(FSM_STATE_QUIT);				
			}
			break;			
		case FSM_STATE_AUTO_MEASURE_1:  // 自动测量
			if(sig->id == FSM_SIG_UI_MEASURE){
				SetUIChange(0);
				Acquisition(FALSE);
				SetState(FSM_STATE_IDEL);				
			}else if(sig->id == FSM_SIG_START_MEASURE){
				Acquisition(TRUE);
				Measure(TRUE);
				SetState(FSM_STATE_AUTO_MEASURE_2);				
			}else if(sig->id == FSM_SIG_QUIT){
				OnFSMStateQuitInFun();				
				SetState(FSM_STATE_QUIT);				
			}			
			break;			
		case FSM_STATE_QUIT:			
			// OnFSMStateQuitOutFun();
			break;
	}
}

int SendFSMSig(FSM_ID *fsmId,FSM_SIG_ID sigId)
{   
	FSM_SIG *sig = &g_fsmSig[sigId];
// lock
	OnFSMSig(fsmId,sig);
// unlock
	return 0;
}
int  ChartInit(TaskHandle task)
{
	int error = 0;
	//uInt32 numChannels;

	g_info.task = task;
	g_info.timerCtrl = MAIN_TIMER_MEASURE;
	g_info.numSampsPerChan = SAMPLE_RATE * SAMPLE_BUF_READ_TIME; // 采集缓冲区每次读取的长度：采样频率*读取间隔时间
	g_info.timeout = 10;
	g_info.chartPanel = panelGraph;
	g_info.chartCtrl = GRAPH_GRAPH;
	//errChk(DAQmxGetTaskAttribute (task, DAQmx_Task_NumChans, &numChannels));
	//SetCtrlAttribute (panelMain, MAIN_STRIPCHART, ATTR_NUM_TRACES, numChannels);
	GetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_LEFT_YAXIS ,&g_info.scalingMode[0],&g_info.min[0],&g_info.max[0]);
	GetAxisScalingMode(g_info.chartPanel,g_info.chartCtrl,VAL_RIGHT_YAXIS,&g_info.scalingMode[1],&g_info.min[1],&g_info.max[1]);

Error:
	return error;
}

int DesktopGuiInit()
{
	int error = 0;	
	int attr;
	
	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_TOP,&attr);
	SetPanelAttribute(panelGraph,ATTR_TOP,attr);
	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_LEFT,&attr);
	SetPanelAttribute(panelGraph,ATTR_WIDTH,attr);
	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_HEIGHT,&attr);
	SetPanelAttribute(panelGraph,ATTR_HEIGHT,attr);

	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_TOP,&attr);
	SetPanelAttribute(panelUserData,ATTR_TOP,attr);
	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_LEFT,&attr);
	SetPanelAttribute(panelUserData,ATTR_WIDTH,attr);	
	GetCtrlAttribute(panelMain,MAIN_DESK_DECORATION,ATTR_HEIGHT,&attr);
	SetPanelAttribute(panelUserData,ATTR_HEIGHT,attr);	
	
Error:
	/* clean up */
	return error;
}

void RunGUI(TaskHandle task)
{
	int error = 0;

	//errChk (panelHandle = LoadPanel (0, "ZZJ.uir", PANEL));
#if defined(PORTABLE)
	errChk (panelMain    = LoadPanel (0, "Portable.uir", MAIN));
#else	
	errChk (panelMain    = LoadPanel (0, "Portable.uir", MAIN_DESK));
    SetPanelAttribute (panelMain, ATTR_WINDOW_ZOOM, VAL_MAXIMIZE);	
	//SetPanelPos(panelMain,0,0);
#endif	
	errChk (panelMeasure = LoadPanel (panelMain	, "Portable.uir", MEASURE));
	errChk (panelSystem  = LoadPanel (panelMain	, "Portable.uir", SYSTEM));
	errChk (panelGraph   = LoadPanel (panelMain , "Portable.uir", GRAPH));

	errChk (panelPrint   = LoadPanel (panelMain , "Portable.uir", P_PRINT));
	errChk (panelUserData = LoadPanel(panelMain , "Portable.uir", USER_DATA));
	errChk (panelUserDataCmd = LoadPanel(panelMain,"Portable.uir",USER_CMD));

#if !defined(PORTABLE)	
	errChk (DesktopGuiInit());
#endif
	errChk (ChartInit(task));

	/* display the panel and run the user interface */
	TBDisplayTime(); // 更新界面：当前时间

	errChk(EnableCursor(panelGraph, GRAPH_RB_FORCE));	 // 更新界面，让光标与界面保持一致。隐藏力光标
	errChk(EnableCursor(panelGraph, GRAPH_RB_CURRENT));  // 更新界面，让光标与界面保持一致。隐藏电流光标
	errChk (DisplayPanel (panelMain));
	errChk (DisplayPanel (panelGraph));
	//QueueUserEvent(EVENT_COMMIT,panelGraph,GRAPH_RB_FORCE);
	errChk (RunUserInterface ());

Error:
	/* clean up */
	if (panelMain > 0)
		DiscardPanel (panelMain);
}


/// HIFN The main entry-point function.
#if defined(PORTABLE)
int main (int argc, char *argv[])
{
#if 1
	int DAQmxError = 0;
	TaskHandle task;
	/* initialize and load resources */
	if(InitCVIRTE (0, argv, 0)==0)
		return -1;

	DAQmxErrChk(DAQmxLoadTask("RailwaySwitch",&task));
	InitThreadQueue();
	RunGUI(task);
Error:
	if(DAQmxError!=0){
		MessagePopup("错误","未连接硬件，程序将退出。");
	}
	UninitUserThread(g_ThreadControls,sizeof(g_ThreadControls)/sizeof(ThreadControl));	   // 退出线程,注意线程调用的函数，如DAQmxStopTask。	如果有这类函数，则本函数必须放在它结束之前。
	DAQmxClearTask(task);		
	UninitThreadQueue();
#endif
	return 0;
}
#else

int main (int argc, char *argv[])
{
	/* initialize and load resources */
	if(InitCVIRTE (0, argv, 0)==0)
		return -1;

	RunGUI(NULL);

Error:
	return 0;
}
#endif

//==============================================================================
// UI callback function prototypes

int CVICALLBACK OnExitPrograme (int panel, int control, int event,
								void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		case EVENT_LEFT_CLICK:
		case EVENT_LEFT_DOUBLE_CLICK:
#if defined(PORTABLE) 			
			//g_info.running = RUN_MODEL_QUIT;
			SendFSMSig(&g_fsmID,FSM_SIG_QUIT);
#else
			QuitUserInterface (0);			
#endif
			
	}
	return 0;
}




int CVICALLBACK OnStartMeasure (int panel, int control, int event,
								void *callbackData, int eventData1, int eventData2)
{
	if (event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		SendFSMSig(&g_fsmID,FSM_SIG_UI_MEASURE);
	}
	return 0;
}


#if defined(PORTABLE)
int Acquisition(BOOL isStart)
{
	int errNumber = 0;
	static BOOL isRunning = FALSE; // 防止多次启动/停止

	if(isStart){ // 开始采集
		if(isRunning != TRUE){
			isRunning = TRUE;
			//errNumber = DAQmxStartTask(g_info.task);		
			FlushQueue_Sample();
			UserThread(THREAD_CMD_CREATE_AND_RUN,&g_ThreadControls[0]);
		}
	}else{		 // 停止采集
		if(isRunning != FALSE){
			isRunning = FALSE;
			//errNumber = DAQmxStopTask(g_info.task);
			UserThread(THREAD_CMD_QUIT,&g_ThreadControls[0]);
		}
	}
	return errNumber;
}

// 开始测量
void Measure(int isStart)
{
	int DAQmxError = 0;
	if(isStart)   // 开始采集
	{
		//g_info.running = RUN_MODEL_RUNNING;
		//DAQmxErrChk(DAQmxStopTask(g_info.task));
		//DAQmxErrChk(DAQmxStartTask(g_info.task));
		g_info.startTick = GetTickCount();
		// 启动定时器。用于显示/采集。
		SetCtrlAttribute(panelMain,MAIN_TIMER_MEASURE,ATTR_ENABLED,TRUE);
	}
	else  // 停止采集
	{
		//g_info.running = RUN_MODEL_STOP;
		//g_info.workModel = WORK_MODEL_MANUAL;
		g_info.processedTick = 0;
		//DAQmxErrChk(DAQmxStopTask(g_info.task));
		// 停止定时器(用于显示/采集)
		SetCtrlAttribute(panelMain,MAIN_TIMER_MEASURE,ATTR_ENABLED,FALSE);		
	}
Error:
	DAQmxReportErr(DAQmxError);
	return;
}
#else
// 开始测量
void Measure(int isStart)
{
	return;
}
int Acquisition(BOOL isStart) 
{
	return 0;
}
#endif


int UpdateUserData(int panel,BOOL isSave)
{
	char str[255];
	time_t testTime;

	if(panel == panelMeasure)
	{
		if(isSave)
		{
			// 转辙机型号
			GetCtrlVal(panel,MEASURE_SWITCH_MODEL,str);
			strcpy(g_UserData.switchModel,str);
			// 力传感器类型
#if 0
			GetCtrlVal(panel,USER_DATA_FORCE_SENSOR_TYPE,str);
#else
			if(!strcmp(g_UserData.switchModel,"S700K-C"))
			{
				strcpy(str,"φ25mm");
			}
			else
			{
				strcpy(str,"φ22mm");
			}
#endif
			strcpy(g_UserData.forceSensorType,str);
			// 测试日期
			time(&testTime);
			g_UserData.TestTime	= testTime;
		}
		else
		{
			// 转辙机型号
			strcpy(str,g_UserData.switchModel);
			SetCtrlVal(panel,MEASURE_SWITCH_MODEL,str);
		}
	}
	else if(panel == panelUserData)
	{
		if(isSave)
		{
			// 文件名
			GetCtrlVal(panel,USER_DATA_FILENAME,str);
			strcpy(g_FileName,str);
			
			// 转辙机型号
			GetCtrlVal(panel,USER_DATA_SWITCH_MODEL,str);
			strcpy(g_UserData.switchModel,str);

			// 转辙机编号
			GetCtrlVal(panel,USER_DATA_SWITCH_NUM,str);
			strcpy(g_UserData.switchNum,str);
			// 测试地点
			GetCtrlVal(panel,USER_DATA_TEST_ADDR,str);
			strcpy(g_UserData.TestAddr,str);
			// 测试人
			GetCtrlVal(panel,USER_DATA_TEST_PERSON,str);
			strcpy(g_UserData.TestPerson,str);

		}
		else    // 读取数据并显示在面板上
		{
			// 文件名
			//strcpy(str,g_FileName);
			CreatWaveformName(str,g_UserData.switchNum);
			SetCtrlVal(panel,USER_DATA_FILENAME,str);
			// 转辙机型号
			strcpy(str,g_UserData.switchModel);
			SetCtrlVal(panel,USER_DATA_SWITCH_MODEL,str);

			// 力传感器类型
			strcpy(str,g_UserData.forceSensorType);
			SetCtrlVal(panel,USER_DATA_FORCE_SENSOR_TYPE,str);
			// 转辙机编号
			strcpy(str,g_UserData.switchNum);
			SetCtrlVal(panel,USER_DATA_SWITCH_NUM,str);
			// 测试地点
			strcpy(str,g_UserData.TestAddr);
			SetCtrlVal(panel,USER_DATA_TEST_ADDR,str);
			// 测试人
			strcpy(str,g_UserData.TestPerson);
			SetCtrlVal(panel,USER_DATA_TEST_PERSON,str);
			// 测试日期
			testTime = g_UserData.TestTime;
			strftime(str,sizeof(str),"%Y年%m月%d日 %H:%M:%S",localtime(&testTime));
			SetCtrlVal (panel, USER_DATA_TEST_TIME, str);
		}
	}
	else
	{
		ERR1("UpdateUserData参数panel错误，panel:%d.",panel);
	}
	return 0;
}

// 测量参数设置，确认开始测量按钮
int CVICALLBACK OnMeasureStart (int panel, int control, int event,
								void *callbackData, int eventData1, int eventData2)
{
	if (event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		// 转辙机用户数据
		if(control == MEASURE_PIC_AUTO_MEASURE)  // 自动测量
		{
			//g_info.workModel = WORK_MODEL_AUTO;
			SendFSMSig(&g_fsmID,FSM_SIG_AUTO_MEASURE);
		}
		else    // 手动测量
		{
			//Measure(TRUE);
			SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
		}
		HidePanel(panel);
	}

	return 0;
}

int CVICALLBACK OnSystemSet (int panel, int control, int event,
							 void *callbackData, int eventData1, int eventData2)
{
	if (event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		HidePanel(panel);
	}
	return 0;
}

int CVICALLBACK OnSystem (int panel, int control, int event,
						  void *callbackData, int eventData1, int eventData2)
{
	if(event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		DisplayPanel(panelSystem);
	}
	return 0;
}

int MeanWin(double sampleData[],ssize_t len,double *val)
{
	double mean = 0;
	double data[len];
	
	memcpy(data,sampleData,len * sizeof(double));
#if 1	
	HanWin(data,len);
	Mean(data,len,&mean);
	*val = 2*mean;
#else
	Mean(data,len,&mean);
	*val = mean;	
#endif	
	return 0;
}

int RMSWin(double sampleData[],ssize_t len,double *val)
{
	double rms = 0;
	double data[len];
	
	memcpy(data,sampleData,len * sizeof(double));
	HanWin(data,len);
	RMS(data,len,&rms);// 有效值
	*val = rms/sqrt(3.0/8.0);
	return 0;
}

int MedianWin(double sampleData[],ssize_t len,double *val)
{
	double median = 0;
	double data[len];
	
	memcpy(data,sampleData,len * sizeof(double));
	HanWin(data,len);
	Median(data,len,&median);// 均值
	*val = 2*median;
	return 0;
}

int MeterDisplay()
{
	double *sampleData;
	double val;
	
	// 显示电压(有效值)，电流(有效值)，力
	ssize_t dataLen = g_info.numSampsPerChan;
	// 电流	
	sampleData = g_Waveform.sampleData[CHANNEL_I];
	if(IsDCSwitchMachine()){
		MeanWin(sampleData,dataLen,&val);  // 平均值电流
	}else{
		RMSWin(sampleData,dataLen,&val);   // 有效值电流		
	}
	//SetCtrlVal(panelMain,MAIN_NUM_I,val);
	SetCtrlAttribute(panelMain,MAIN_NUM_I,ATTR_CTRL_VAL,val);
	
	// 电压
	sampleData = g_Waveform.sampleData[CHANNEL_V];
	if(IsDCSwitchMachine()){
		MeanWin(sampleData,dataLen,&val);		// 平均值电压
	}else{
		RMSWin(sampleData,dataLen,&val);		// 有效值电压		
	}	
	
	//SetCtrlVal(panelMain,MAIN_NUM_V,val);
	SetCtrlAttribute(panelMain,MAIN_NUM_V,ATTR_CTRL_VAL,val);

	// 电压基准
	sampleData = g_Waveform.sampleData[CHANNEL_VREF];
	MedianWin(sampleData,dataLen,&val);// 电压基准的均值。(电压基准为直流电压，均值测量更加准确。)
	//SetCtrlVal(panelMain,MAIN_NUM_VREF,val);
	SetCtrlAttribute(panelMain,MAIN_NUM_VREF,ATTR_CTRL_VAL,val);

	// 力
	sampleData = g_Waveform.sampleData[CHANNEL_FORCE];
	MedianWin(sampleData,dataLen,&val);
	//SetCtrlVal(panelMain,MAIN_NUM_FORCE,val);
	SetCtrlAttribute(panelMain,MAIN_NUM_FORCE,ATTR_CTRL_VAL,val);

	return 0;
}


double MaxDoubleArray(double *array,int len)
{
	double max;
	max = array[0];
	for(int i=0; i<len; i++)
	{
		if(max<array[i])
		{
			max = array[i];
		}
	}
	return max;
}

int isAutoStop(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int len)
{
	int ret = 0;
	double *waveform;

	//WaveformGet(CHANNEL_I,CHANNEL_TYPE_LAST_DATA,&waveform,&len,&x,&increment);	// 电流
	waveform = &sampleData[CHANNEL_V][0];
	if(len!=0)  // 有波形
	{
			waveform = &sampleData[CHANNEL_V][0];
			if(MaxDoubleArray(waveform,len) < 5.0 )  // 采集到的电压
			{
				ret = 1;
			}
	}

	return ret;	
}

int isAutoStart(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int len)
{
	int ret = 0;
	double *waveform;

	//WaveformGet(CHANNEL_I,CHANNEL_TYPE_LAST_DATA,&waveform,&len,&x,&increment);	// 电流
	waveform = &sampleData[CHANNEL_V][0];
	if(len!=0)  // 有波形
	{
#if 0		
		if(MaxDoubleArray(waveform,len) > 1.0 ) // 采集卡电流，不是传感器的输入电压。
		{
			ret = 1;
		}
		else
#endif
		{
			//WaveformGet(CHANNEL_V,CHANNEL_TYPE_LAST_DATA,&waveform,&len,&x,&increment);	// 电压
			waveform = &sampleData[CHANNEL_V][0];
			if(MaxDoubleArray(waveform,len) > 100.0 )  // 采集卡电压，，不是传感器的输入电压。
			{
				ret = 1;
			}
		}
	}

	return ret;
}
///////////////////////////////////////////////////////////////////////////////
// 功能：采样数组转换为测试数据，并放入波形数组。电压电流通道包含真有效值变换。
// 输入：sampleData
// 输出：WaveformSet
///////////////////////////////////////////////////////////////////////////////

ssize_t g_ConvertedLen = 0; // 已经转换过的长度。
int RawData2Waveform(double rawData[SAMPLE_CHANNEL][SAMPLE_RATE * SAMPLE_MAX_TIME],ssize_t len)
{

	int sampNumPerCycle = SAMPLE_RATE/50;  		// 电压、电流每周期采样点数 2500Hz(采样频率)/50Hz(工频频率)  = 50点
	int sampNumPerForceCycle = SAMPLE_RATE/100; // 转换力每周期采样点数。	2500Hz(采样频率)/100Hz(力传感器响应频率) = 25点

#if 1	
	if(len - g_ConvertedLen >= 5*sampNumPerCycle){ // 至少5个周期才能转换有效值
		ssize_t ToConvertLen = len - g_ConvertedLen-4*sampNumPerCycle;// 将要转换的采样点数。原始波形-已经转换的长度-4个周期
		double waveformI [ToConvertLen/sampNumPerCycle]; // 转换的采样点数换算为周期数。每周期算出一个有效值。
		double waveformV [ToConvertLen/sampNumPerCycle];
		double *waveformVref = NULL;
		double waveformFORCE [ToConvertLen/sampNumPerForceCycle];// 转换的采样点数换算为力值。
		
		double *waveform[SAMPLE_CHANNEL]={waveformI,waveformV,waveformVref,waveformFORCE};
		double *data[SAMPLE_CHANNEL];		
		
		data[CHANNEL_I] = &rawData[CHANNEL_I][0];// 电流
		data[CHANNEL_V] = &rawData[CHANNEL_V][0];// 电压
		data[CHANNEL_VREF] = &rawData[CHANNEL_VREF][0];// 电压基准
		data[CHANNEL_FORCE] = &rawData[CHANNEL_FORCE][0];// 力
		int index = g_ConvertedLen;
		int i=0;
		for(; index<len-4*sampNumPerCycle; index+=sampNumPerCycle,++i)
		{
			if(IsDCSwitchMachine()){ // 直流转辙机
				MeanWin(data[CHANNEL_I]+index,g_info.numSampsPerChan,&waveform[CHANNEL_I][i]);
				MeanWin(data[CHANNEL_V]+index,g_info.numSampsPerChan,&waveform[CHANNEL_V][i]);							
			}else{					 // 交流转辙机
				RMSWin(data[CHANNEL_I]+index,g_info.numSampsPerChan,&waveform[CHANNEL_I][i]);
				RMSWin(data[CHANNEL_V]+index,g_info.numSampsPerChan,&waveform[CHANNEL_V][i]);				
			}
			Median(data[CHANNEL_FORCE]+index	,25,&waveform[CHANNEL_FORCE][2*i]  );	// 转换力.前25点。每秒100次，每次2500Hz/100Hz = 25点平均值
			Median(data[CHANNEL_FORCE]+index+25	,25,&waveform[CHANNEL_FORCE][2*i+1]);	// 转换力.后25点。						

		}
		initialX = g_ConvertedLen/50;
		initialForce = g_ConvertedLen/25;
		initialVRef = g_ConvertedLen;
		WaveformSet(CHANNEL_I,&waveform[CHANNEL_I][0],i,initialX);
		WaveformSet(CHANNEL_V,&waveform[CHANNEL_V][0],i,initialX);
		WaveformSet(CHANNEL_VREF,&data[CHANNEL_VREF][0],ToConvertLen,initialVRef);
		WaveformSet(CHANNEL_FORCE,&waveform[CHANNEL_FORCE][0],2*i,initialForce);
		g_ConvertedLen = index;
	}
#endif
#if 0
	// 电压基准
	data[CHANNEL_VREF] = rawData[CHANNEL_VREF];
	WaveformSet(CHANNEL_VREF,data[CHANNEL_VREF],g_info.numSampsPerChan,initialVRef);
	initialVRef +=g_info.numSampsPerChan;

	// 转换力
	
	data[CHANNEL_FORCE] = rawData[CHANNEL_FORCE];
	int numMedian = 10;//0.1s/(1/100Hz)
	for(int i=0; i<numMedian; ++i)
	{
		Median(data[CHANNEL_FORCE]+i*25,25,&waveform[CHANNEL_FORCE][i]);//每次25点中间值
	}
	WaveformSet(CHANNEL_FORCE,waveform[CHANNEL_FORCE],numMedian,initialForce);
	initialForce +=numMedian;
#endif
	return 0;
}



///////////////////////////////////////////////////////////////////////////////
// 功能：单次采样数据转换为测试数据，并放入波形数组。电压电流通道包含真有效值变换。
// 输入：sampleData
// 输出：WaveformSet
///////////////////////////////////////////////////////////////////////////////
int SampleData2Waveform(int isRuning,double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE])
{

	int sampNumPerCycle =SAMPLE_RATE/50;  // 每周期采样点数 2500Hz/50Hz=50点
	int numRMS = g_info.numSampsPerChan/sampNumPerCycle;	//5次。250/50 = 5
	static double waveform[SAMPLE_RATE/10];
	double* data;

	if(isRuning)  // 测量中
	{
	}
	else 		   // 只显示，未测量，不增加数据
	{
		initialX = 0;
		initialVRef = 0;
		initialForce = 0;
	}

	// 电流
#if 0	
	data = sampleData[CHANNEL_I];
	for(int i=0; i<numRMS; ++i)
	{
		RMS(data+i*sampNumPerCycle,sampNumPerCycle,&waveform[i]);
	}
	WaveformSet(CHANNEL_I,waveform,numRMS,initialX);
#else
	data = g_Waveform.sampleData[CHANNEL_I];
	RMSWin(data,g_info.numSampsPerChan,&waveform[0]);
	WaveformSet(CHANNEL_I,waveform,1,initialX/5.0);
#endif


	// 电压
#if 1	
	data = sampleData[CHANNEL_V];
	for(int i=0; i<numRMS; ++i)
	{
		RMS(data+i*sampNumPerCycle,sampNumPerCycle,&waveform[i]);
	}

	WaveformSet(CHANNEL_V,waveform,numRMS,initialX);
	initialX +=  numRMS;
#else
	data = sampleData[CHANNEL_V]; 
	RMSWin(data,g_info.numSampsPerChan,&waveform[0]);
	WaveformSet(CHANNEL_V,waveform,1,initialX/5.0);	
#endif


	// 电压基准
	data = sampleData[CHANNEL_VREF];
	WaveformSet(CHANNEL_VREF,data,g_info.numSampsPerChan,initialVRef);
	initialVRef +=g_info.numSampsPerChan;

	// 转换力
	data = sampleData[CHANNEL_FORCE];
	int numMedian = 10;//0.1s/(1/100Hz)
	for(int i=0; i<numMedian; ++i)
	{
		Median(data+i*25,25,&waveform[i]);//每次25点中间值
	}
	WaveformSet(CHANNEL_FORCE,waveform,numMedian,initialForce);
	initialForce +=numMedian;// 电压参考

	return 0;
}
int SampleData2RawData(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int len)
{
	double *dataStart;
	for(int i=0;i<SAMPLE_CHANNEL;i++){
		dataStart = &g_Waveform.rawData[i][g_Waveform.rawDataLen];
		memcpy(dataStart,sampleData[i],len*sizeof(double));
	}
	g_Waveform.rawDataLen += len;
	return 0;
}

#if defined(PORTABLE) 
#if defined(SIMULATE_INPUT) // 软件模拟输入数据
#define SIMULATE_FREQUENCY 20.0/2500 // 归一化频率。波形频率/采样频率。50Hz/2500Hz
#define SIMULATE_PHASE 2.0  // 初始相位。单位°。360为一周。
int ReadMeasure(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int *length)
{
	int32 DAQmxError = DAQmxSuccess; 	
	float64 *data = NULL;
	uInt32 numChannels = 4;//4通道
	uInt32 dataSize;
	double phase = SIMULATE_PHASE;

	Sleep(100);  // 模拟硬件采集卡两次采集的时间间隔0.1s
	dataSize = numChannels * g_info.numSampsPerChan;
	DAQmxNullChk(data = (float64 *)malloc (dataSize * sizeof(float64)));

	int len = g_info.numSampsPerChan;
	int channel=0;
	// 电流
	SineWave(len,3.0*sqrt(2),SIMULATE_FREQUENCY	,&phase,data+len*channel);
	channel ++;
	// 电压
	SineWave(len,160.0*sqrt(2),SIMULATE_FREQUENCY	,&phase,data+len*channel);
	channel ++;	
	// 电压基准 2.5V
	Set1D(data+len*channel,len,2.5);
	channel ++;	
	// 力
	//SineWave(len,10,1.0/2500	,&phase,data+len*channel);
	Set1D(data+len*channel,len,5.0);

	for(channel=0; channel<numChannels; channel++)
	{
		memcpy(sampleData[channel],data+len*channel,len*sizeof(double));
	}

	*length = len;
Error:
	if (data)
		free (data);
	return 0;
}
#else   // 使用硬件采集卡



int ReadMeasure(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int *length)
{
	int32 DAQmxError = DAQmxSuccess;
	float64 *data = NULL;
	uInt32 numChannels, dataSize;



	// Example Core: Allocate a buffer of appropriate size.  If you know
	// the necessary buffer size at compile time you can declare an array
	// of fixed size on the stack instead of allocating one.

	DAQmxErrChk(DAQmxGetTaskAttribute (g_info.task, DAQmx_Task_NumChans, &numChannels));
	dataSize = numChannels * g_info.numSampsPerChan;
	DAQmxNullChk(data = (float64 *)malloc (dataSize * sizeof(float64)));

	// Example Core: Read the specified number of samples from each channel.

	DAQmxErrChk(DAQmxReadAnalogF64 (g_info.task, g_info.numSampsPerChan, g_info.timeout,
									DAQmx_Val_GroupByChannel, data, dataSize, NULL, 0));
#if 0
	for(int channel=0; channel<numChannels; channel++)
	{
		memcpy(g_Waveform.sampleData[channel],data,g_info.numSampsPerChan*sizeof(double));
	}
#else
	int len = g_info.numSampsPerChan;
	for(int channel=0; channel<numChannels; channel++)
	{
		memcpy(sampleData[channel],data+len*channel,len*sizeof(double));
	}
	*length = len;
	
#endif

Error:
	if (data)
		free (data);
	if (DAQmxFailed(DAQmxError))
	{
		DAQmxReportErr(DAQmxError);
		//StopCB_StartReadWfmLoopStop (g_info.chartPanel, info->stopCtrl, EVENT_COMMIT, callbackData, 0, 0);
	}
	return DAQmxError;
}
#endif
#endif


#if defined(PORTABLE)
int ReadMeasureAndToWaveform()
{

	int32 DAQmxError = DAQmxSuccess;
	float64 *data = NULL;
	uInt32 numChannels, dataSize;

	//double xIncrement = 0.02;
	double val;

	//static float64 values[10*1000*1000];

	// Example Core: Allocate a buffer of appropriate size.  If you know
	// the necessary buffer size at compile time you can declare an array
	// of fixed size on the stack instead of allocating one.

	DAQmxErrChk(DAQmxGetTaskAttribute (g_info.task, DAQmx_Task_NumChans, &numChannels));
	dataSize = numChannels * g_info.numSampsPerChan;
	DAQmxNullChk(data = (float64 *)malloc (dataSize * sizeof(float64)));

	// Example Core: Read the specified number of samples from each channel.
#if 0
	DAQmxErrChk(DAQmxReadAnalogF64 (g_info.task, g_info.numSampsPerChan, g_info.timeout,
									DAQmx_Val_GroupByScanNumber, data, dataSize, NULL, 0));
#else
	DAQmxErrChk(DAQmxReadAnalogF64 (g_info.task, g_info.numSampsPerChan, g_info.timeout,
									DAQmx_Val_GroupByChannel, data, dataSize, NULL, 0));
#endif
	//PlotStripChart (g_info.chartPanel, g_info.chartCtrl, data, dataSize, 0, 0, VAL_DOUBLE);
//int PlotWaveform (int panelHandle, int controlID, void *yArray, size_t numberOfPoints, int yDataType, double yGain, double yOffset, double initialX, double xIncrement, int plotStyle, int pointStyle, int lineStyle, int pointFrequency, int color);

	// 显示电压(有效值)，电流(有效值)，力
	RMS(data,g_info.numSampsPerChan,&val);// 电流
	SetCtrlVal(panelMain,MAIN_NUM_I,val);

	RMS(data+g_info.numSampsPerChan,g_info.numSampsPerChan,&val);// 电压
	SetCtrlVal(panelMain,MAIN_NUM_V,val);

	RMS(data+g_info.numSampsPerChan*2,g_info.numSampsPerChan,&val);// 电压基准
	SetCtrlVal(panelMain,MAIN_NUM_VREF,val);

	RMS(data+g_info.numSampsPerChan*3,g_info.numSampsPerChan,&val);// 动作力
	SetCtrlVal(panelMain,MAIN_NUM_FORCE,val);

#if 0
	xIncrement =  = 1.0/3000;
	PlotWaveform (g_info.chartPanel, g_info.chartCtrl, data, g_info.numSampsPerChan, VAL_DOUBLE, 1.0f, 0.0f, initialX, xIncrement, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_BLUE);
	PlotWaveform (g_info.chartPanel, g_info.chartCtrl, data+g_info.numSampsPerChan, g_info.numSampsPerChan, VAL_DOUBLE, 1.0f, 0.0f, initialX, xIncrement, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_RED);
	//PlotWaveform (g_info.chartPanel, g_info.chartCtrl, data+g_info.numSampsPerChan*2, g_info.numSampsPerChan, VAL_DOUBLE, 1.0f, 0.0f, initialX, xIncrement, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_WHITE);
	initialX +=  xIncrement*g_info.numSampsPerChan;
#else
	int sampNumPerCycle =SAMPLE_RATE/50;  // 每周期采样点数 2500Hz/50Hz=50点
	int numRMS = g_info.numSampsPerChan/sampNumPerCycle;	//5次。250/50 = 5
	double waveform[SAMPLE_RATE/10];

	// 电流
	for(int i=0; i<numRMS; ++i)
	{
		RMS(data+i*sampNumPerCycle,sampNumPerCycle,&waveform[i]);
	}

#if 0
	PlotWaveform (g_info.chartPanel, g_info.chartCtrl, waveform, numRMS, VAL_DOUBLE, 1.0f, 0.0f,
				  initialX, xIncrement, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_WHITE);
#else
	WaveformSet(CHANNEL_I,waveform,numRMS,initialX);
#endif
	// 电压
	float64 *pdata = data + g_info.numSampsPerChan;
	for(int i=0; i<numRMS; ++i)
	{
		RMS(pdata+i*sampNumPerCycle,sampNumPerCycle,&waveform[i]);
	}
#if 0
	PlotWaveform (g_info.chartPanel, g_info.chartCtrl, waveform, numRMS, VAL_DOUBLE, 1.0f, 0.0f,
				  initialX, xIncrement, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_RED);
#else
	WaveformSet(CHANNEL_V,waveform,numRMS,initialX);

#endif

	// 电压基准
	pdata = data + g_info.numSampsPerChan*2;
	WaveformSet(CHANNEL_VREF,pdata,g_info.numSampsPerChan,initialVRef);
	initialVRef +=g_info.numSampsPerChan;// 电压参考

	// 转换力
	pdata = data + g_info.numSampsPerChan*3;//
	int numMedian = 10;//0.1s/(1/100Hz)
	for(int i=0; i<numMedian; ++i)
	{
		Median(pdata+i*25,25,&waveform[i]);//每次25点中间值
	}
	WaveformSet(CHANNEL_FORCE,waveform,numMedian,initialForce);
	initialForce +=numMedian;// 电压参考


	initialX +=  numRMS;

	//initialX +=  xIncrement*numRMS;

#endif
Error:
	if (data)
		free (data);
	if (DAQmxFailed(DAQmxError))
	{
		DAQmxReportErr(DAQmxError);
		//StopCB_StartReadWfmLoopStop (g_info.chartPanel, info->stopCtrl, EVENT_COMMIT, callbackData, 0, 0);
	}
	return 0;
}
#endif 

void PlotData(CHANNEL_TYPE chanType,int panel,int graphCtrl)  // 电流:红色，电压：绿色，电压基准：蓝色，转换力：白色
{
	double* waveform;
	double x;
	int len;
	double increment;
	int plotHandle;
	
	TimeMeasure(4,TM_START);
	SetCtrlAttribute(panel, graphCtrl,ATTR_REFRESH_GRAPH,0);TimeMeasure(4,TM_STOP);TimeMeasure(5,TM_START);
	WaveformGet(CHANNEL_I,chanType,&waveform,&len,&x,&increment);	TimeMeasure(5,TM_STOP);// 绘制电流波形图
	if(len)
	{
				TimeMeasure(6,TM_START);
		plotHandle = PlotWaveform (panel, graphCtrl, waveform, len, VAL_DOUBLE, 1.0f, 0.0f,
								   x, increment, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_RED);
				TimeMeasure(6,TM_STOP);  TimeMeasure(7,TM_START);
		if(chanType == CHANNEL_TYPE_ALL_DATA){
			SetPlotAttribute (panel, graphCtrl, plotHandle, ATTR_PLOT_YAXIS, VAL_RIGHT_YAXIS);
		}
		TimeMeasure(7,TM_STOP);
	}
#if 0
	WaveformGet(CHANNEL_V,chanType,&waveform,&len,&x,&increment);	// 绘制电压波形图
	if(len)
	{
		plotHandle = PlotWaveform (panel, graphCtrl, waveform, len, VAL_DOUBLE, 1.0f, 0.0f,
								   x, increment, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_WHITE);
		//SetPlotAttribute (panel, graphCtrl, plotHandle, ATTR_PLOT_YAXIS, VAL_RIGHT_YAXIS);
	}
#endif
	
#if 0
	WaveformGet(CHANNEL_VREF,chanType,&waveform,&len,&x,&increment);// 绘制电压基准波形图
	if(len)
	{
		plotHandle = PlotWaveform (panel, graphCtrl, waveform, len, VAL_DOUBLE, 1.0f, 0.0f,
								   x, increment, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_BLUE);
		//SetPlotAttribute (panel, graphCtrl, plotHandle, ATTR_PLOT_YAXIS, VAL_RIGHT_YAXIS);
	}
#endif
	TimeMeasure(8,TM_START);
	WaveformGet(CHANNEL_FORCE,chanType,&waveform,&len,&x,&increment);// 绘制转换力波形图
	if(len)
	{
		PlotWaveform (panel, graphCtrl, waveform, len, VAL_DOUBLE, 1.0f, 0.0f,
					  x, increment, VAL_THIN_LINE, VAL_SOLID_SQUARE, VAL_SOLID, 1, VAL_GREEN);
	}
	TimeMeasure(8,TM_STOP);TimeMeasure(9,TM_START);
	SetCtrlAttribute(panel, graphCtrl,ATTR_REFRESH_GRAPH,1); TimeMeasure(9,TM_STOP);

}

#if defined(PORTABLE)
#define MDAASVer 4
#if MDAASVer == 0
int MeasureDisplayAndAutoStart()
{
	if(g_info.running == RUN_MODEL_STOP) // 没有在测量，只显示当前值，不绘制曲线
	{
		if(g_info.workModel == WORK_MODEL_AUTO)
		{
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			//SampleData2Waveform(FALSE,g_Waveform.sampleData);
			MeterDisplay();  // 在仪表面板显示测量值
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				Measure(TRUE);
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
			}
			else
			{
			}
		}
	}
	else if(g_info.running == RUN_MODEL_RUNNING)     // 正在测量
	{
	
		int maxTime_ms = 15.1*1000;	   // 只测量15秒内的数据
		int curTick = GetTickCount();
		if(curTick > g_info.startTick + maxTime_ms)
			curTick = g_info.startTick + maxTime_ms;
		int n = (curTick - g_info.startTick - g_info.processedTick)/100;
		g_info.processedTick = ((curTick - g_info.startTick)/100)*100;

		for(int i=0; i<n; i++)
		{
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
		}
		RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);
		MeterDisplay();
		PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);		
		if(g_info.processedTick >= maxTime_ms) // 只测量maxTime 毫秒内的数据
		{
			Measure(FALSE); // 停止采集
		}
		else
		{
		}
	}
	else if(g_info.running == RUN_MODEL_QUIT)    //quit
	{
		Measure(FALSE); // 停止采集
		QuitUserInterface (0);
	}
	return 0;
}
#elif MDAASVer == 1
// 经过的时间折合采样/显示次数。
int ElapseTimeToRunN(double maxTime,int *n)
{
	int maxTime_ms = 15.1*1000;	   // 只测量15秒内的数据
	int curTick = GetTickCount();
	if(curTick > g_info.startTick + maxTime_ms)
		curTick = g_info.startTick + maxTime_ms;
	g_info.processedTick = ((curTick - g_info.startTick)/100)*100;		
	*n = (curTick - g_info.startTick - g_info.processedTick)/100;
	return 0;
}

int MeasureDisplayAndAutoStart()
{
	if(g_info.running == RUN_MODEL_STOP) // 没有在测量，只显示当前值，不绘制曲线
	{
		if(g_info.workModel == WORK_MODEL_AUTO)
		{
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			//SampleData2Waveform(FALSE,g_Waveform.sampleData);
			MeterDisplay();  // 在仪表面板显示测量值
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				Measure(TRUE);
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
			}
			else
			{
			}
		}
	}
	else if(g_info.running == RUN_MODEL_RUNNING)     // 正在测量
	{
#if 1	
		int maxTime_ms = 15.1*1000;	   // 只测量15秒内的数据
		int curTick = GetTickCount();
		if(curTick > g_info.startTick + maxTime_ms)
			curTick = g_info.startTick + maxTime_ms;
		int n = (curTick - g_info.startTick - g_info.processedTick)/100;
		g_info.processedTick = ((curTick - g_info.startTick)/100)*100;
#else
		int n;
		ElapseTimeToRunN(15.1,&n);
#endif
		for(int i=0; i<n; i++)
		{
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
		}
		RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);
		MeterDisplay();
		PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);		
		if(g_info.processedTick >= maxTime_ms) // 只测量maxTime 毫秒内的数据
		{
			Measure(FALSE); // 停止采集
		}
		else
		{
		}
	}
	else if(g_info.running == RUN_MODEL_QUIT)    //quit
	{
		Measure(FALSE); // 停止采集
		QuitUserInterface (0);
	}
	return 0;
}
#elif MDAASVer == 2
// 经过的时间折合采样/显示次数。
int ElapseTimeToRunN(double maxTime_s,int *n)
{
	int maxTime_ms = maxTime_s*1000;	   // 秒转换为毫秒。只测量maxTime_s秒内的数据
	int curTick = GetTickCount();
	if(curTick > g_info.startTick + maxTime_ms)
		curTick = g_info.startTick + maxTime_ms;
	*n = (curTick - g_info.startTick - g_info.processedTick)/100;
	g_info.processedTick = ((curTick - g_info.startTick)/100)*100;		
	return 0;
}
int time_start_ms;
int time_stop_ms;
int time_elapse_ms;
int MeasureDisplayAndAutoStart()
{
	FSM_ID fsm;
	GetFSMState(&g_fsmID,&fsm);	
	switch(fsm.state){
		case FSM_STATE_AUTO_MEASURE_2:	// 测量中		
			double maxTime_s = 15.1;	   // 只测量15秒内的数据
			int n;
			ElapseTimeToRunN(maxTime_s,&n);
			if(n>0){ 
				TimeMeasure(0,TM_START);
				for(int i=0; i<n; i++)
				{
					ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
					SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				}
				TimeMeasure(0,TM_STOP);TimeMeasure(1,TM_START);
				MeterDisplay();TimeMeasure(1,TM_STOP);TimeMeasure(2,TM_START);
				RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);TimeMeasure(2,TM_STOP);TimeMeasure(3,TM_START);
				PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);TimeMeasure(3,TM_STOP);
			}
			if(g_info.processedTick >= maxTime_s*1000) // 只测量maxTime 秒内的数据
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE); // 停止采集  
			}			
			break;			
		case FSM_STATE_AUTO_MEASURE_1:  // 等待输入信号，自动测量。只显示当前值，不绘制曲线
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			MeterDisplay();  // 在仪表面板显示测量值
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				//Measure(TRUE);
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
			}
			break;			
		default :
			break;
	}	
	return 0;
}
#elif MDAASVer == 3
// 经过的时间折合采样/显示次数。
int ElapseTimeToRunN(double maxTime_s,int *n)
{
	int maxTime_ms = maxTime_s*1000;	   // 秒转换为毫秒。只测量maxTime_s秒内的数据
	int curTick = GetTickCount();
	if(curTick > g_info.startTick + maxTime_ms)
		curTick = g_info.startTick + maxTime_ms;
	*n = (curTick - g_info.startTick - g_info.processedTick)/100;
	g_info.processedTick = ((curTick - g_info.startTick)/100)*100;		
	return 0;
}
int time_start_ms;
int time_stop_ms;
int time_elapse_ms;
int MeasureDisplayAndAutoStart()
{
	FSM_ID fsm;
	GetFSMState(&g_fsmID,&fsm);	
	switch(fsm.state){
		case FSM_STATE_MANUAL_MEASURE:
		case FSM_STATE_AUTO_MEASURE_2:	// 测量中 		
			double maxTime_s = 20.1;	   // 只测量20秒内的数据
			int n;
			ElapseTimeToRunN(maxTime_s,&n);
			if(n>0){ 
				TimeMeasure(0,TM_START);
#if 0				
				for(int i=0; i<n; i++)
				{
					ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
					SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				}
#endif				
				TimeMeasure(0,TM_STOP);TimeMeasure(1,TM_START);
				MeterDisplay();TimeMeasure(1,TM_STOP);TimeMeasure(2,TM_START);
				RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);TimeMeasure(2,TM_STOP);TimeMeasure(3,TM_START);
				PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);TimeMeasure(3,TM_STOP);
			}
			if(g_info.processedTick >= maxTime_s*1000) // 只测量maxTime 秒内的数据
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE); // 停止采集  
			}			
			break;			
		case FSM_STATE_AUTO_MEASURE_1:  // 等待输入信号，自动测量。只显示当前值，不绘制曲线
#if 0			
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			MeterDisplay();  // 在仪表面板显示测量值
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				//Measure(TRUE);
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
			}
#else
			MeterDisplay();  // 在仪表面板显示测量值
#endif
			break;			
		default :
			break;
	}	
	return 0;
}
#elif MDAASVer == 4
// 经过的时间折合采样/显示次数。
int ElapseTimeToRunN(double maxTime_s,int *n)
{
	int maxTime_ms = maxTime_s*1000;	   // 秒转换为毫秒。只测量maxTime_s秒内的数据
	int curTick = GetTickCount();
	if(curTick > g_info.startTick + maxTime_ms)
		curTick = g_info.startTick + maxTime_ms;
	if(n != NULL)
		*n = (curTick - g_info.startTick - g_info.processedTick)/100;
	g_info.processedTick = ((curTick - g_info.startTick)/100)*100;		
	return 0;
}
int time_start_ms;
int time_stop_ms;
int time_elapse_ms;
int MeasureDisplayAndAutoStart()
{
	FSM_ID fsm;
	GetFSMState(&g_fsmID,&fsm);	
	double maxTime_s = 20.1;	// 只测量20秒内的数据
	switch(fsm.state){
		case FSM_STATE_MANUAL_MEASURE:
			ElapseTimeToRunN(maxTime_s,NULL);
			TimeMeasure(0,TM_START);
			if(ReadQueueAllData_Sample()){
				TimeMeasure(0,TM_STOP);TimeMeasure(1,TM_START);
				MeterDisplay();TimeMeasure(1,TM_STOP);TimeMeasure(2,TM_START);
				RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);TimeMeasure(2,TM_STOP);TimeMeasure(3,TM_START);
				PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);TimeMeasure(3,TM_STOP);
			}
			if(g_info.processedTick >= maxTime_s*1000) // 只测量maxTime 秒内的数据
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE); // 停止采集  
			}			
			break;
		case FSM_STATE_AUTO_MEASURE_2:	// 测量中 		
			ElapseTimeToRunN(maxTime_s,NULL);
			TimeMeasure(0,TM_START);
			if(ReadQueueAllData_Sample()){
				TimeMeasure(0,TM_STOP);TimeMeasure(1,TM_START);
				MeterDisplay();TimeMeasure(1,TM_STOP);TimeMeasure(2,TM_START);
				RawData2Waveform(g_Waveform.rawData,g_Waveform.rawDataLen);TimeMeasure(2,TM_STOP);TimeMeasure(3,TM_START);
				PlotData(CHANNEL_TYPE_LAST_DATA,g_info.chartPanel, g_info.chartCtrl);TimeMeasure(3,TM_STOP);
			}			
			if(isAutoStop(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE);
			}			
			if(g_info.processedTick >= maxTime_s*1000) // 只测量maxTime 秒内的数据
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE); // 停止采集  
			}			
			break;			
		case FSM_STATE_AUTO_MEASURE_1:  // 等待输入信号，自动测量。只显示当前值，不绘制曲线
#if 1		
			if(ReadQueue_Sample(g_Waveform.sampleData,&g_Waveform.sampleDataLen)){
				//ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
				MeterDisplay();  // 在仪表面板显示测量值
				if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
				{
					//Measure(TRUE);
					SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
					SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
				}
			}
#else
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			MeterDisplay();  // 在仪表面板显示测量值
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				//Measure(TRUE);
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
			}
#endif
			break;			
		default :
			break;
	}	
	return 0;
}

#endif
#endif 



int CVICALLBACK OnTimer_Measure (int panel, int control, int event,
								 void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		case EVENT_TIMER_TICK:
			#if defined(PORTABLE)  
			MeasureDisplayAndAutoStart();
			#endif 
			break;
	}
	return 0;
}

#define TIME_DISPLAY_VER   3
#if TIME_DISPLAY_VER == 1
int TBDisplayTime( void )
{
	SYSTEMTIME st;
	char  acData[ 32 ];
	GetLocalTime (&st);
	//sprintf (acData, "%04d.%02d.%02d", st.wYear, st.wMonth, st.wDay);
	//SetCtrlVal (g_plMain, MAIN_TM_DATE, acData);

	sprintf (acData, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond );
	SetCtrlVal (panelMain, MAIN_TM_TIME, acData);
	return 0;
}
#elif TIME_DISPLAY_VER == 2
int TBDisplayTime( void )
{
	time_t rawTime;
	struct tm *timeinfo;
	char  acData[ 100 ];
	time(&rawTime);
	timeinfo = localtime(&rawTime);
#if 0
	sprintf (acData, "%02d:%02d:%02d", timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec );
#else
	strftime(acData,sizeof(acData),"%Y-%m-%d %H:%M:%S",timeinfo);
#endif
	SetCtrlVal (panelMain, MAIN_TM_TIME, acData);
	return 0;
}
#else

int TBDisplayTime( void )
{
	time_t rawTime;
	struct tm *timeinfo;
	char  acData[ 50 ];
	time(&rawTime);
	timeinfo = localtime(&rawTime);

	strftime(acData,sizeof(acData),"%H:%M:%S",timeinfo);
#ifdef PORTABLE
	SetCtrlVal (panelMain, MAIN_TM_TIME, acData);
#else
	SetCtrlVal (panelMain, MAIN_DESK_TM_TIME, acData);
#endif	
	return 0;
}
#endif


int CVICALLBACK OnTimer_Watch (int panel, int control, int event,
							   void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		case EVENT_TIMER_TICK:
			TBDisplayTime();
			break;
	}
	return 0;
}

#define DisplayCursorData_Ver 3
#if DisplayCursorData_Ver == 1
int DisplayCursorData(int panel,int control)
{
	int error;
	int activeCursor;
	double cursorX,cursorY;
	char str[20];

	errChk(GetActiveGraphCursor (panel,control,&activeCursor));
	errChk(GetGraphCursor(panel,control,activeCursor,&cursorX,&cursorY));
	if(activeCursor==1)
	{
		sprintf(str,"%.1f(KN)",cursorY);
		SetCtrlVal(panel,GRAPH_FORCE,str);
		sprintf(str,"%.1f(s)",cursorX);
		SetCtrlVal(panel,GRAPH_TIME_FORCE ,str);
	}
	else
	{
		sprintf(str,"%.1f(A)",cursorY);
		SetCtrlVal(panel,GRAPH_CURRENT,str);
		sprintf(str,"%.1f(s)",cursorX);
		SetCtrlVal(panel,GRAPH_TIME_CURRENT ,str);
	}
Error:
	return error;
}
#elif DisplayCursorData_Ver == 2
int DisplayCursorData(int panel,int control)
{
	int error;
	int activeCursor;
	double cursorX,cursorY;
	char str[20];

	errChk(GetActiveGraphCursor (panel,control,&activeCursor));
	errChk(GetGraphCursor(panel,control,activeCursor,&cursorX,&cursorY));
	if(activeCursor==1)
	{
		sprintf(str,"%.2f",cursorY);
		SetCtrlVal(panel,GRAPH_FORCE,str);
		sprintf(str,"%.2f",cursorX);
		SetCtrlVal(panel,GRAPH_TIME_FORCE ,str);
	}
	else
	{
		sprintf(str,"%.2f",cursorY);
		SetCtrlVal(panel,GRAPH_CURRENT,str);
		sprintf(str,"%.2f",cursorX);
		SetCtrlVal(panel,GRAPH_TIME_CURRENT ,str);
	}
	
	//sprintf(str,"%.2f",cursorX);	
	//SetCtrlVal(panel,TIME_DELTA,str);
Error:
	return error;
}
#elif DisplayCursorData_Ver == 3 
int DisplayCursorData(int panel,int control)
{
	int error;
	int activeCursor;
	
	double cursorX[2],cursorY[2];
	char str[20];

	errChk(GetActiveGraphCursor (panel,control,&activeCursor));
	errChk(GetGraphCursor(panel,control,1,&cursorX[0],&cursorY[0]));
	errChk(GetGraphCursor(panel,control,2,&cursorX[1],&cursorY[1]));
	if(activeCursor==1)
	{
		sprintf(str,"%.2f",cursorY[0]);
		SetCtrlVal(panel,GRAPH_FORCE,str);
		sprintf(str,"%.2f",cursorX[0]);
		SetCtrlVal(panel,GRAPH_TIME_FORCE ,str);
	}
	else
	{
		sprintf(str,"%.2f",cursorY[1]);
		SetCtrlVal(panel,GRAPH_CURRENT,str);
		sprintf(str,"%.2f",cursorX[1]);
		SetCtrlVal(panel,GRAPH_TIME_CURRENT ,str);
	}
	
	sprintf(str,"%.2f",cursorX[1]-cursorX[0]);	
	SetCtrlVal(panel,GRAPH_TIME_DELTA,str);
Error:
	return error;
}
#endif
int CVICALLBACK OnGraph (int panel, int control, int event,
						 void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		//case EVENT_COMMIT:
		case EVENT_VAL_CHANGED:
			DisplayCursorData(panel,control);
			break;
	}
	return 0;
}

int EnableCursor(int panel, int control)
{
	int CursorNumber;
	int val;
	GetCtrlVal(panel,control,&val);
	if(control == GRAPH_RB_FORCE)
	{
		CursorNumber = 1;
	}
	else
	{
		CursorNumber = 2;
	}
	if(val) // 打开光标
	{
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CURSOR_ENABLED,val);
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CROSS_HAIR_STYLE,VAL_LONG_CROSS);
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CURSOR_POINT_STYLE,VAL_SOLID_SQUARE);
	}
	else    // 关闭光标
	{
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CURSOR_ENABLED,val);
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CROSS_HAIR_STYLE,VAL_NO_CROSS);
		SetCursorAttribute(panel,GRAPH_GRAPH,CursorNumber,ATTR_CURSOR_POINT_STYLE,VAL_NO_POINT);
	}

	//更新显示
	//DisplayCursorData(panel,GRAPH_GRAPH);
	return 0;
}
int CVICALLBACK OnCursor (int panel, int control, int event,
						  void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		case EVENT_COMMIT:
			EnableCursor(panel,control);
			break;
	}
	return 0;
}


#if 1
// 读取数据并显示在打印画布上面
// 输入：
// 		data:用户数据
//		panel：面板句柄
// 输出：
// 		无
// 返回：
// 		0：成功
int PrintUserData(UserData *data,int panel)
{
	char str[50];
	time_t testTime;


	// 取消表格控件的当前选择
	//SetTableSelection (panel, P_PRINT_TABLE, VAL_EMPTY_RECT );
	// 取消表格控件的蓝色框
	//SetActiveTableCell (panel, P_PRINT_TABLE, MakePoint (0, 0));

	SetCtrlAttribute(panel,P_PRINT_TABLE,ATTR_HIDE_HILITE,FALSE);
	// 转辙机型号
	sprintf(str," %s",data->switchModel);
	//SetCtrlVal(panel,MEASURE_SWITCH_MODEL,str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 1), str);

	// 力传感器类型
	sprintf(str," %s",data->forceSensorType);
	//SetCtrlVal(panel,MEASURE_FORCE_SENSOR_TYPE,str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 3), str);
	// 转辙机编号
	sprintf(str," %s",data->switchNum);
	//SetCtrlVal(panel,MEASURE_SWITCH_NUM,str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 2), str);
	// 测试地点
	sprintf(str," %s",data->TestAddr);
	//SetCtrlVal(panel,MEASURE_TEST_ADDR,str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 5), str);
	// 测试人
	sprintf(str," %s",data->TestPerson);
	//SetCtrlVal(panel,MEASURE_TEST_PERSON,str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 6), str);
	// 测试日期
	testTime = data->TestTime;
	strftime(str,sizeof(str)," %Y年%m月%d日 %H:%M:%S",localtime(&testTime));
	//SetCtrlVal (panel, MEASURE_TEST_TIME, str);
	SetTableCellVal (panel, P_PRINT_TABLE, MakePoint (2, 4), str);
	return 0;
}

#endif

// 用户点击打印按钮
int CVICALLBACK OnPrint (int panel, int control, int event,
						 void *callbackData, int eventData1, int eventData2)
{
	if (event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		PrintUserData(&g_UserData,panelPrint);
		PlotData(CHANNEL_TYPE_ALL_DATA,panelPrint, P_PRINT_GRAPH);

		SetPrintAttribute(ATTR_ORIENTATION,VAL_PORTRAIT);
		//SetPrintAttribute(ATTR_SYSTEM_PRINT_DIALOG_ONLY,TRUE);
		int ret = PrintPanel(panelPrint,NULL,1,VAL_VISIBLE_AREA ,TRUE);
		INFO1("PrintPanel return:%d",ret);
	}
	return 0;
}

int SaveData(char *fileName)
{
	FILE *fp;

	if((fp = fopen(fileName,"wb"))==NULL)
	{
		goto Error;
	}
#if 1
	fwrite((void*)&g_UserData,sizeof(g_UserData),1,fp);// 保存转辙机用户输入的数据
#endif
	fwrite((void*)&g_Waveform,sizeof(g_Waveform),1,fp);// 保存转辙机采集到的波形

Error:
	if(fp)fclose(fp);
	return 0;
}

int LoadWaveform(char *fileName)
{
	FILE *fp;
	if((fp = fopen(fileName,"rb"))==NULL)
	{
		goto Error;
	}
#if 1
	fread((void*)&g_UserData,sizeof(g_UserData),1,fp);// 读取转辙机用户输入的数据
#endif
	fread((void*)&g_Waveform,sizeof(g_Waveform),1,fp);// 读取转辙机采集到的波形
Error:
	if(fp)fclose(fp);
	return 0;
}

int CVICALLBACK OnSaveData (int panel, int control, int event,
							void *callbackData, int eventData1, int eventData2)
{
	if(event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		SendFSMSig(&g_fsmID,FSM_SIG_SAVE);
		//UpdateUserData(panelUserData,FALSE);
		//DisplayPanel(panelUserData);
	}
	return 0;
}


char* GetWaveFormPath()
{
	static char path[MAX_PATHNAME_LEN];
	GetProjectDir(path);
	sprintf(path,"%s\\waveform",path);
	return path;
}

int LoadALLData()
{
	char fileName[MAX_PATHNAME_LEN];
	// 取得用户要载入的文件名

	if(FileSelectPopupEx(GetWaveFormPath(),"*.vif","","打开波形文件",VAL_LOAD_BUTTON ,0,1,fileName) != VAL_NO_FILE_SELECTED)
	{
		// 打开该文件载入数据
		LoadWaveform(fileName);
		// 以该数据绘制曲线
		UpdatePlot();
	}
	else   // 用户放弃
	{
	}
	return 0;
}
int CVICALLBACK OnOpenData (int panel, int control, int event,
							void *callbackData, int eventData1, int eventData2)
{
	if(event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		//LoadALLData();
		SendFSMSig(&g_fsmID,FSM_SIG_LOAD);
	}
	return 0;
}


int SaveForceDataAsCSV(char *fileName)
{
	FILE *fp;
	int retVal = 0;

	if((fp = fopen(fileName,"w"))==NULL)
	{
		goto Error;
	}

	for(int i = 0; i < g_Waveform.rawDataLen; i++)
	{
		fprintf(fp,"%f,",g_Waveform.rawData[CHANNEL_FORCE][i]); 	// 保存转辙机采集到的原始数据
	}	
	
	fprintf(fp,"\n");
			
	for(int i = 0; i < g_Waveform.forceLen; i++)
	{
		fprintf(fp,"%f,",g_Waveform.force[i]); 		// 保存转辙机采集后转换的波形
	}
	
	
Error:
	if(fp){
		fclose(fp);
	}else{
		retVal = 1;	 // 错误
	}
	return retVal;
}

int SaveALLData()
{
	char fullFileName[MAX_PATHNAME_LEN];
	
#if 0
	strcpy(fullFileName,GetWaveFormPath());

	//CreatWaveformName(fullFileName);
	if(FileSelectPopupEx(GetWaveFormPath(),"*.dat","","保存波形",VAL_SAVE_BUTTON,1,1,fullFileName)
			!= VAL_NO_FILE_SELECTED)
	{
		
		SaveData(fullFileName);  // 存入文件，采集到的转辙机数据和用户数据
	}
#else	
	sprintf(fullFileName,"%s\\%s",GetWaveFormPath(),g_FileName);
	SaveData(fullFileName);  // 存入文件，采集到的转辙机数据和用户数据
	
	SaveForceDataAsCSV("force.csv");
#endif	
	return 0;
}

int CVICALLBACK OnSaveUserData (int panel, int control, int event,
								void *callbackData, int eventData1, int eventData2)
{
	if(event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		HidePanel(panel);					// 隐藏用户数据命令面板   //HidePanel(panelUserDataCmd)
		HidePanel(panelUserData);			// 隐藏用户数据面板
		
		UpdateUserData(panelUserData,TRUE);
		SaveALLData();
	}
	return 0;
}

int CVICALLBACK OnCancelSaveUserData (int panel, int control, int event,
									  void *callbackData, int eventData1, int eventData2)
{
	if(event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		HidePanel(panel);
		HidePanel(panelUserData);
	}
	return 0;
}

int CVICALLBACK OnMeasureCancel (int panel, int control, int event,
								 void *callbackData, int eventData1, int eventData2)
{
	if (event == EVENT_LEFT_CLICK || event == EVENT_LEFT_DOUBLE_CLICK)
	{
		HidePanel(panel);
	}
	return 0;
}

int CVICALLBACK ONSwitchNumChange (int panel, int control, int event,
								   void *callbackData, int eventData1, int eventData2)
{
	switch (event)
	{
		case EVENT_COMMIT:
			char fileName[MAX_PATHNAME_LEN];
			char switchNum[50];
			GetCtrlVal(panel,USER_DATA_SWITCH_NUM,switchNum);
			//strcpy(g_UserData.switchNum,str);
			//UpdateUserData(panel,TRUE);
			CreatWaveformName(fileName,switchNum);
			SetCtrlVal(panel,USER_DATA_FILENAME,fileName);
			break;
	}
	return 0;
}

///////////////////////////////////////////////
// 线程回调函数.采集线程
#if defined(PORTABLE)
#define Acquire_Ver 2
#if Acquire_Ver == 1
void Acquire()
{
	FSM_ID fsm;
	GetFSMState(&g_fsmID,&fsm);	
	switch(fsm.state){
		case FSM_STATE_MANUAL_MEASURE:
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);						
			break;
		case FSM_STATE_AUTO_MEASURE_2:	// 测量中	
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);			
			if(isAutoStop(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				SendFSMSig(&g_fsmID,FSM_SIG_STOP_MEASURE);
			}			
			break;			
		case FSM_STATE_AUTO_MEASURE_1:  // 等待输入信号，自动测量。只显示当前值，不绘制曲线	
			ReadMeasure(g_Waveform.sampleData,&g_Waveform.sampleDataLen);
			if(isAutoStart(g_Waveform.sampleData,g_Waveform.sampleDataLen))
			{
				SampleData2RawData(g_Waveform.sampleData,g_Waveform.sampleDataLen);
				SendFSMSig(&g_fsmID,FSM_SIG_START_MEASURE);
			}
			break;
	}
}
#elif Acquire_Ver == 2
void Convert2JiaoChuo(double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE],int sampleDataLen,double dat[SAMPLE_RATE][SAMPLE_CHANNEL])
{
	for(int i=0;i<sampleDataLen;i++){
		dat[i][0] = sampleData[0][i];
		dat[i][1] = sampleData[1][i];
		dat[i][2] = sampleData[2][i];
		dat[i][3] = sampleData[3][i];
		//WriteQueue(sampleData[i],1);
	}	
}
void Acquire()
{
	static double sampleData[SAMPLE_CHANNEL][SAMPLE_RATE];// 单次从采集卡读取的原始数据。每通道容量为1秒(SAMPLE_RATE)
	// 通道：0：电流，1：电压，2：基准，3：力
	static int    sampleDataLen;// 单次读取的原始数据的长度	
	//static double dat[SAMPLE_RATE][SAMPLE_CHANNEL];
	
	ReadMeasure(sampleData,&sampleDataLen);
	WriteQueue_Sample(sampleData,sampleDataLen); 
	//Convert2JiaoChuo(sampleData,sampleDataLen,dat);
	//WriteQueue(dat,sampleDataLen); 
}
#endif
int CVICALLBACK ThreadAcquire(void *functionData)
{
	ThreadControl *ctl = (ThreadControl*)functionData;
	
	DAQmxStartTask(g_info.task);
	while (!ctl->quit) {
		if (ctl->suspend) { // 暂停当前线程的运行
			SuspendThread (GetCurrentThread ());
			ctl->suspend = 0;
		}
		Acquire();// 采集数据
		//Analyze(. . .);// 处理采集到的数据		
	}
	DAQmxStopTask(g_info.task);
	DAQmxWaitUntilTaskDone(g_info.task,DAQmx_Val_WaitInfinitely);//等待超时设置为1秒
	return 0;
}
#else
int CVICALLBACK ThreadAcquire(void *functionData)
{
	return 0;
}
#endif

int CVICALLBACK OnSwitchDataAndWave (int panel, int control, int event,
									 void *callbackData, int eventData1, int eventData2)
{   
static int isDisplayInfo = 1;  // 显示信息
	switch(event) 
	{
		case  EVENT_LEFT_CLICK:
	 	case  EVENT_LEFT_DOUBLE_CLICK:	  
			if(isDisplayInfo){	// 显示信息
				SetCtrlAttribute(panel,MAIN_DESK_PIC_WAVE_DATA,ATTR_LABEL_TEXT,"显示波形");
				UpdateUserData(panelUserData,FALSE);
				DisplayPanel(panelUserData);				// 显示保存面板
			}else{				// 显示波形
				SetCtrlAttribute(panel,MAIN_DESK_PIC_WAVE_DATA,ATTR_LABEL_TEXT,"显示信息");
				HidePanel(panelUserData);
			}
			isDisplayInfo = !isDisplayInfo;
		break;
	}
	return 0;
}
