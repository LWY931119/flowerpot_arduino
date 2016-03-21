#include<stdio.h>
#include<string.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Time.h>
#include <DS1307RTC.h>
#define PT550_Pin 1//光线传感器pinA1
#define moi_Pin 3//土壤湿度传感器A3
#define LM35D_Pin 2//温度传感器pinA2
bool f = false;//判断发过来的是不是完整命令
char commandline[40];//命令行字符串
char command[10];//命令
char index[30];//命令的参数
bool mode = true;//模式判断，true是普通模式，false是设置模式
const long sjjg = 1800000;//时间间隔 30min
const int lcdjg = 10000;//10s刷新一次lcd
unsigned long SDUp; //记录上次写入SD卡的时间
unsigned long LCDUp;//记录上次显示LCD的时间
LiquidCrystal_I2C lcd(0x27, 16, 2); // set the LCD address to 0x27 for a 16 chars and 2 line display
#define SDPIN 4 //  SD卡片选位置
#define BTstate 2 //蓝牙状态位
/*********配置Co2传感器***********/
#define MG_PIN   (0)     //Co2传感器位置(A0)
#define DC_GAIN  (8.5)   //放大倍数
/***********************Software Related Macros************************************/
#define READ_SAMPLE_INTERVAL (50)    //定义取样次数
#define READ_SAMPLE_TIMES (5)     //定义每次取样间隔时间

/**********************Application Related Macros**********************************/
//These two values differ from sensor to sensor. user should derermine this value.
#define  ZERO_POINT_VOLTAGE  (0.324) //define the output of the sensor in volts when the concentration of CO2 is 400PPM
#define  REACTION_VOLTGAE    (0.020) //define the voltage drop of the sensor when move the sensor from air into 1000ppm CO2

/*****************************Globals***********************************************/
float  CO2Curve[3]  =  {2.602,ZERO_POINT_VOLTAGE,(REACTION_VOLTGAE/(2.602-3))};   
//two points are taken from the curve. 
//with these two points, a line is formed which is
//"approximately equivalent" to the original curve.
//data format:{ x, y, slope}; point1: (lg400, 0.324), point2: (lg4000, 0.280) 
//slope = ( reaction voltage ) / (log400 –log1000) 
/*****************************  MGRead *********************************************
Input:   mg_pin - analog channel
Output:  output of SEN0159
Remarks: This function reads the output of SEN0159读取二氧化碳传感器的电压值
分READ_SAMPLE_TIMES次读取，每次间隔READ_SAMPLE_INTERVAL的时间
************************************************************************************/ 
float MGRead(int mg_pin)
{
  int i;
  float v=0;
  for (i=0;i<READ_SAMPLE_TIMES;i++) 
  {
    v += analogRead(mg_pin);
    delay(READ_SAMPLE_INTERVAL);
  }
  v = (v/READ_SAMPLE_TIMES) *5/1024 ;
  return v;  
}

/*****************************  MQGetPercentage **********************************
Input:   volts   - SEN-000007 output measured in volts
pcurve  - pointer to the curve of the target gas
Output:  ppm of the target gas
Remarks: By using the slope and a point of the line. The x(logarithmic value of ppm) 
of the line could be derived if y(MG-811 output) is provided. As it is a 
logarithmic coordinate, power of 10 is used to convert the result to non-logarithmic 
value.
根据公式把电压值换算成二氧化碳浓度值
************************************************************************************/ 
int  MGGetPercentage(float volts, float *pcurve)
{
  return pow(10, ((volts/DC_GAIN)-pcurve[1])/pcurve[2]+pcurve[0]);
}

/************************  readtxt  *****************************************
读取SD卡中txt文件的内容并通过蓝牙发送
TODO：发送完成以后是否要删除文件呢？
****************************************************************************/
void readtxt()
{
  File myFile;
  myFile = SD.open("sensor.txt");
  if (myFile) 
  {
    Serial.println("sensor.txt:");

    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    // close the file:
    myFile.close();
  } 
  else 
  {
    // if the file didn't open, print an error:
    Serial.println("$error: opening sensor.txt&");
  }
}

/******************************* settime *********************************
设置DS1307 RTC的时间，参数格式2016-3-15,13:17:12
**************************************************************************/
void settime(char* datetime)
{
  int Year,Month,Day,Hour,Minute,Second;
  sscanf(datetime,"%d-%d-%d,%d:%d:%d",&Year,&Month,&Day,&Hour,&Minute,&Second);
  tmElements_t tm;
  tm.Year = CalendarYrToTm(Year);
  tm.Month = Month;
  tm.Day = Day;
  tm.Hour = Hour;
  tm.Minute = Minute;
  tm.Second = Second;
  RTC.write(tm);
}



void writeSD()
{
  String time1 = "";
  tmElements_t tm1;
  if (RTC.read(tm1)) {
    time1 = "+" + String(tmYearToCalendar(tm1.Year))+  "-" + String(tm1.Month) + "-" +String(tm1.Day)+ " "+String(tm1.Hour)+":"+String(tm1.Minute)+":"+String(tm1.Second)+"&";
    }
    else{
      time1 = "&";
    }
    SDUp = millis();
    File myFile;
    myFile = SD.open("sensor.txt", FILE_WRITE);
    // if the file opened okay, write to it:
    if (myFile) 
    {
    	/****************** 温度传感器 ******************/
        //int LM35=analogRead(LM35D_Pin);//读取LM35D_Pin端口模拟值
        //float temperature=(float)LM35/1024*500;//计算结果
        float tem = MGRead(LM35D_Pin)*100;
        myFile.print("$text:Tem:");
        myFile.print(tem,2);
        myFile.println(time1);
        /**************** 光照度传感器 ******************/
      	//int val;
      	//val=analogRead(PT550_Pin);   //connect grayscale sensor to Analog 0
     	float val = MGRead(PT550_Pin);
     	int lig = val*1200;//0-5v对应 1-6000lux系数1200
     	myFile.print("$text:Lig:");
      	myFile.print(lig,DEC);//print the value to serial 
      	myFile.println(time1);
      	/*************** 二氧化碳传感器 *****************/
      	int percentage;
      	float volts;   
      	volts = MGRead(MG_PIN);
      	myFile.print( "$text:SEN0159:" );
      	myFile.print(volts); 
      	myFile.print( "V " );
       volts = volts + 1.3;
     	percentage = MGGetPercentage(volts,CO2Curve);
      	myFile.print("CO2:");        
      	myFile.print(percentage);
      	myFile.println(time1);
      	/*************** 土壤湿度传感器 *****************/
      	myFile.print("$text:Moi:");
      	myFile.print(analogRead(moi_Pin));
      	myFile.println(time1);

      // close the file:
      myFile.close();
      Serial.println("$success:write text done&");
    } else {
      // if the file didn't open, print an error:
      Serial.println("$error:opening sensor.txt&");
    }
 
}
/*****************************commandCase******************************
根据发送过来的命令返回一个整数，用来switch
***********************************************************************/
int commandCase()
{
  if(strcmp(command,"settime") == 0)return 1;//设置时间
  if(strcmp(command,"changemode") == 0)return 2;//切换模式
  if(strcmp(command,"gettime") == 0)return 3;//获取时间
  if(strcmp(command,"f") == 0)return 4;//读取文件
  if(strcmp(command,"b") == 0)return 5;//读取温度传感器
  if(strcmp(command,"c") == 0)return 6;//读取二氧化碳传感器
  if(strcmp(command,"d") == 0)return 7;//读取光照传感器
  if(strcmp(command,"e") == 0)return 8;//土壤湿度传感器
}
/******************************writeLCD*******************************
更新lcd的显示内容
*********************************************************************/
void writeLCD()
{
  LCDUp = millis();
	lcd.clear();
    float tem = MGRead(LM35D_Pin)*100;
	lcd.print("T:");
	lcd.print(tem);
    int percentage;
    float volts;   
    volts = MGRead(MG_PIN);
   volts = volts + 1.3;
      percentage = MGGetPercentage(volts,CO2Curve);
    lcd.print(" C02:");
	lcd.print(percentage);
    lcd.setCursor(0,1);
    float val = MGRead(PT550_Pin);
    int lig = val*1200;//0-5v对应 1-6000lux系数1200
	lcd.print("L:");
	lcd.print(lig);
    int moi = analogRead(moi_Pin);
    lcd.print("  M:");
	lcd.print(moi);
}

void setup() {
  	// put your setup code here, to run once:
 	pinMode(BTstate,INPUT);
	Serial.begin(9600);
  	while (!Serial) {
    	; // wait for serial port to connect. Needed for Leonardo only
  	}
   /**************************初始化LCD***************************/
   lcd.init(); // initialize the lcd 
    lcd.backlight(); //Open the backlight
    lcd.clear();
    lcd.print("setup...");
    delay(2000);
    
  	/***********************初始化SD卡********************/
  	//Serial.print(" SD card...");
  	if (!SD.begin(SDPIN)) {
   		 lcd.clear();
       lcd.print("SD error!");
       delay(10000);
  	}
  	//Serial.println("initialization done.");
  	File myFile;
  	myFile = SD.open("sensor.txt", FILE_WRITE);
  	if (myFile) {
    	
       lcd.clear();
       lcd.print("creat sensor.txt successly.");
       delay(10000);
  	}
  	else{
    	lcd.clear();
      lcd.print("SD error!");
  	}
  	myFile.close();
  	/**********************记录SD卡时间*********************/
  	SDUp = millis();
  	LCDUp = millis();
  	writeLCD();
    writeSD();
}

void loop() {
  	if(millis()- SDUp > sjjg) {
   	 	writeSD();
  	}
  	if(millis() - LCDUp >lcdjg){
  		writeLCD();
  	}
    String commandstring = "";
    char c;
    while(Serial.available() > 0){
      c = char(Serial.read());
      delay(2);
      while( c != '@'){
        if(Serial.available() > 0){
          c = char(Serial.read());
          delay(2);
        }
        else break;
      }
      if(Serial.available() > 0){
        c = char(Serial.read());
        delay(2);
      }
      while(c != '$'){
        if(Serial.available() > 0){
          commandstring += c;
          c = char(Serial.read());
          delay(2);
        }
        else break;
      }
      if(c == '$'){
        f = true;
        while(Serial.read() >= 0){}
      }
    }//end while(Serial.available() > 0)

    if(commandstring.length() > 0 && f ){
        commandstring.toCharArray(commandline,sizeof(commandline));
        sscanf(commandline,"%[a-z]->%[a-z0-9A-Z:,-]",command,index);
        int tem = commandCase();
        
        switch(tem){
          case 1:if(mode == false){
              Serial.print("$settime:");
              Serial.print(index);
              Serial.println("&");
              settime(index);
              }
              else{
                Serial.println("$error:please change mode&");
              }
              break;
          case 2:mode = !mode;
          if(mode){
              Serial.print("$changemod:to true&");                    
          }
          else{
              Serial.print("$changemod:to false&");
          }
          break;
          case 3:tmElements_t tm1;
          if (RTC.read(tm1)) {     
              Serial.print("$gettime:");
              Serial.print(tmYearToCalendar(tm1.Year));
              Serial.write('-');
              Serial.print(tm1.Month);
              Serial.write('-');
              Serial.print(tm1.Day);
              Serial.print(" ");
              Serial.print(tm1.Hour);
              Serial.write(':');
              Serial.print(tm1.Minute);
              Serial.write(':');
              Serial.print(tm1.Second);
              Serial.println("&");
          } 
          break;
          case 4:if(mode){
            float tem = MGRead(LM35D_Pin)*100;
            Serial.print("$Tem:");
            Serial.print(tem,2);
            Serial.println("&");
                }
                else{
                  Serial.println("$error:please change mode&");
                }
                break;
          case 5: if(mode){
              Serial.println("$Open:file&");
              readtxt();
              //SD.remove("sensor.txt");
              delay(100);
                }
                else{
                  Serial.println("$error:please change mode&");
                }
                break;
          case 6:if(mode){
              int percentage;
              float volts;   
              volts = MGRead(MG_PIN);
              volts = volts + 1.3;
              Serial.print( "$SEN0159:" );
              Serial.print(volts); 
              Serial.print( "V " );
              percentage = MGGetPercentage(volts,CO2Curve);
              Serial.print("CO2:");        
              Serial.print(percentage);
              Serial.println("&");
              }
              else{
                Serial.println("$error:please change mode&");
              }
              break;
          case 7:if(mode){
            float val = MGRead(PT550_Pin);
          int lig = val*1200;//0-5v对应 1-6000lux系数1200
            Serial.print("$Lig:");
            Serial.print(lig,DEC);//print the value to serial 
            Serial.println("&");
            }
              else{
                Serial.println("$error:please change mode&");
              }
              break;
          case 8:if(mode){
              Serial.print("$Moi:");
              Serial.print(analogRead(moi_Pin));
              Serial.println("&");
              }
              else{
                  Serial.println("$error:please change mode&");
              }   
              break;
      }//end of switch
      commandstring = "";
      f = false;
    }//end of if
}
