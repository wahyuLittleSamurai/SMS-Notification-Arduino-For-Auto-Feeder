//#### input library yg digunakan // 
#include <Wire.h>
#include "RTClib.h"
#include "thermistor.h"
#include "HardwareSerial.h"
#include <SoftwareSerial.h>
#include <EEPROM.h>

SoftwareSerial SIM800(2, 3); // deklarasi komunikasi antara sim dengan mikro

#define NTC_PIN   A1  //pin untuk sensor suhu NTC
#define resetPin  4 //pin untuk reset SIM
#define waterLvl  5 //pin water level yg bawah
#define waterLvl2 A3 //pin water level yg atas
    

#define minSuhu 27  //nilai minimal suhu yg diatur
#define maxSuhu 28  //nilai maksimal suhu yg diatur
#define jamOn   16  //jam on
#define menOn   00  //menit on
#define jamOff  17  //jam off
#define menOff  46  //menit off
#define maxPh   8 //nilai maksimal PH
#define minPh   5 //nilai min PH
#define jamOnNutrisi  6//jam 
#define lamaOnValveNutrisi  5 //satuan detik
#define hariKuras 0 //0: minggu, 1: senin... 6: sabtu


int relayPin[] = {8,9,10,11,12,13};

THERMISTOR thermistor(NTC_PIN,        // Analog pin
                      10000,          // Nominal resistance at 25 ºC
                      3950,           // thermistor's beta coefficient
                      10000);         // Value of the series resistor

// Global temperature reading
uint16_t temp;

RTC_DS1307 rtc;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
unsigned char jam, menit, detik, tanggal, bulan, tahun;

//## variable yg digunakan di program
int maxTime,counterCommand;
boolean found = false;
boolean autoReset = false;
boolean newSms = false;
boolean startParsing = false;
boolean startSplit = false;
String dataSms;
String valSplit[7];
long int valPhInterval;
static float pHValue;
float pHValueVolt;
int samplingInterval = 50;

boolean lamp, pump, valve, heatOrFan, isiNutrisi, sudahKuras;
int autoOrManual=0;

int counterReadSIM;
int errorCounter;

#define SensorPin A0            //pH meter Analog output to Arduino Analog Input 2
#define Offset 3.80            //deviation compensate
#define LED 13
#define samplingInterval 20
#define printInterval 800
#define ArrayLenth  40    //times of collection
int pHArray[ArrayLenth];   //Store the average value of the sensor feedback
int pHArrayIndex=0;    

boolean isiSekarang = false;
boolean kurasSekarang = false;

void setup () {
  
  Serial.begin(9600);
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  for(int i=0; i<=5; i++)
  {
    pinMode(relayPin[i], OUTPUT);       // pengaturan relay sebagai pin output
    digitalWrite(relayPin[i], HIGH);    //non aktifkan relay
  }

  pinMode(waterLvl, INPUT_PULLUP);    // pin water level dijadikan input pullup karena dia akan aktif jika nilai 0
  pinMode(waterLvl2, INPUT_PULLUP);
  
  SIM800.begin(9600);                 // kecepatan baudrate komunikasi
  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, HIGH);
  
  Serial.println("Please Wait...");
  digitalWrite(resetPin, LOW);
  delay(200);
  digitalWrite(resetPin, HIGH);


       
  delay(15000);
  //## program untuk setting sim800 sebagai mode text dan menghapus seluruh SMS yg ada, agar tidak error
  while(counterCommand<=3)
  {
    switch(counterCommand)
    {
      case 0: atCommand("AT",1,"OK");break;
      case 1: atCommand("AT+CMGF=1",1,"OK");break;
      case 2: atCommand("AT+CMGL=\"ALL\",0",2,"OK");break;
      case 3: atCommand("AT+CMGD=1,4",1,"OK");break;
    }
  }
  counterCommand = 0;

  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
}

void loop () {

  //## otomatis
  millis();
  DateTime now = rtc.now();
  readTime(); //panggil function baca waktu
  readTemp();
  readPH();
  delay(250);
  
  if(SIM800.available())
  {
    if(SIM800.find("+CMTI:")) //jika ada sms masuk, maka...
    {
      Serial.println("New Message Received"); //tampilkan di serial monitor
      newSms = true;
      counterCommand = 0;
      dataSms = "";
      maxTime = 0;
    }
  }
  while(newSms)// baca SMS yg masuk
  {
    Serial.println();
    Serial.println("read sms");
    Serial.println();
    atCommandRead("AT+CMGL=\"ALL\",0");
  }
  while(startSplit) //parsing isi sms yg masuk
  {
    Serial.println();
    Serial.println("startSplit");
    Serial.println();
    parseData(dataSms,"#"); //panggil function parseData, untuk memisahkan isi dari SMS dengan tanda #
    
    // untuk cek status seluruh sensor
    if(valSplit[1] == "cek")  
    {
      atCommand("AT+CMGD=1,4",5,"OK");
      Serial.println("Kirim SMS status");
      startSplit = false;
      atCommand("AT+CMGF=1",1,"OK");  //set mode text untuk sim900
      atCommand("AT+CMGS=\"085850474633\"",1,">");//setting nomer tujuan  
      atCommand("Suhu="+String(temp)+" Ph= "+String(pHValue)+"",1,">");//isi dari sms
      Serial.println("Mengirim Char Ctrl+Z / ESC untuk keluar dari menu SMS");
      SIM800.println((char)26);//ctrl+z untuk keluar dari menginputkan isi sms
      delay(2000); //tahan selama 1 detik
     
    }
    else
    {
      atCommand("AT+CMGD=1,4",5,"OK");
      startSplit = false;
    }
  }

  //otomatis **************************************************************************
  if(temp < minSuhu)  //heat ON, kipas OFF
  {
    digitalWrite(relayPin[0], LOW);
    heatOrFan = true;
    Serial.println("Heat ON");
  }
  if(temp >= maxSuhu) //kipas ON, heat OFF
  {
    digitalWrite(relayPin[0], HIGH);
    heatOrFan = false;
    Serial.println("Kipas ON");
  }
  if((jam >= jamOn && menit >= menOn) && (jam <= jamOff && menit <= menOff))//jika jam dan menit sesuai dengan setting maka lampu ON
  {
    digitalWrite(relayPin[1], LOW);
    lamp = true;
    Serial.println("Lampu ON");
  }
  else  //jika tidak maka lampu OFF
  {
    
      digitalWrite(relayPin[1], HIGH);
      lamp = false;
      Serial.println("Lampu OFF");
    
  }
  
  if(jam == jamOnNutrisi && now.dayOfTheWeek() == hariKuras)  //jika jam nutrisi sesuai dengan yg diinginkan 
  {
    if(!isiNutrisi) //jika belum pernah memberikan nutrisi maka...
    {
      digitalWrite(relayPin[3], LOW); //nyalakan valve 
      Serial.println("Valve ON");
      delay(lamaOnValveNutrisi);  //tunggu sampai jeda
      digitalWrite(relayPin[3], HIGH);
      isiNutrisi = true;
    }
  }
  if(jam != jamOnNutrisi)
  {
    Serial.println("Valve OFF");
    isiNutrisi = false;
  }
  
  
  Serial.println("#######################");
  Serial.println();
}

//func untuk perintah setting SIM800
void atCommand(String iCommand, int timing, char myText[])
{
  String onOff = (String)myText;  //jadikan data array char myText[] menjadi string
  
  Serial.println("###Start###");
  Serial.print("Command Ke ->>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ");
  Serial.print(counterCommand);
  Serial.print("Kirim=>");Serial.println(iCommand);
  while(timing>maxTime)   //selama waktu timeout belum terpenuhi maka...
  {
    SIM800.println(iCommand); //kirim data ke sim800 sesuai dengan perintah
    if(SIM800.find(myText)) //jika mikrokontroller menemukan data balasan sesuai dengan yg diinginkan maka perintah sudah terpenuhi
    {
      found = true;
      break;
    }
    Serial.print(maxTime);Serial.print(",");
    maxTime++;
  }
  if(found == true)
  {
    autoReset = false;
    
    counterCommand++;
   
    Serial.println("==============================>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> oke");
  }
  else
  {
      Serial.print("\nautoReset=true\n");
      autoReset = true;
      Serial.println("--------============>>>>>>>> AT Command Error");
      Serial.println("--------============>>>>>>>> Proses reset");
      digitalWrite(resetPin, LOW);
      delay(200);
      digitalWrite(resetPin, HIGH);
      delay(15000);
      counterCommand = 0;
  }
  if(counterCommand >=100)
  {
    counterCommand = 3;
  }
  found = false;
  maxTime=0;
  Serial.println("***end***");
  
}
//func untuk perintah untuk baca isi dari SMS sim800
void atCommandRead(String iCommand)
{
  balikLagi:
  counterReadSIM = 0;
  SIM800.println(iCommand); //kirim perintah ke sim800 sesuai dengan variabl iCommand
  
  while(1)
  {
    if(SIM800.available())  //jika ada balasan dari sim800 maka...
    {
      char c = SIM800.read(); //simpan kedalam char c
      Serial.write(c);
      if(c == '#' && !startParsing) //jika isi sms tersebut ditemukan data dengan character '#' maka....
      {
        startParsing = true;  //tanda mulai parsing isi sms
      }
      if(c == '!')  //jika tanda tersebut adalah tanda seru berarti merupakan akhir dari SMS
      {
        //Serial.println("   end SMS");
        startSplit = true;
        startParsing = false;
        newSms = false;
        break;
      }
      
      if(startParsing)  //jika start parsing sudah dimulai, simpan data c tersebut kedalam variable String dataSms
      {
        dataSms += c;
      }
    }
    if(counterReadSIM > 5000)
    {
      Serial.println("errrooooorrrrrrrr");
      delay(5000);
      if(errorCounter > 5)
      {
        atCommand("AT+CMGD=1,4",5,"OK");
        Serial.println("Kirim SMS status");
        startSplit = false;
        atCommand("AT+CMGF=1",1,"OK");  //set mode text untuk sim900
        atCommand("AT+CMGS=\"085850474633\"",1,">");//setting nomer tujuan  
        atCommand("Error... SMS lagi",1,">");//isi dari sms
        Serial.println("Mengirim Char Ctrl+Z / ESC untuk keluar dari menu SMS");
        SIM800.println((char)26);//ctrl+z untuk keluar dari menginputkan isi sms
        delay(2000); //tahan selama 1 detik
        errorCounter = 0;
        
        break;
      }
      errorCounter++;
      goto balikLagi;
    }
    counterReadSIM++;
  }
}
//func untuk parsing isi dari SMS
void parseData(String text, String key)
{
  int countSplitSecond=0;
  int lastIndexSecond=0;
  text += key;

  Serial.println(text); 
  for(int j = 0; j < text.length(); j++)//lakukan parsing sebanyak panjang dari isi sms
  {
    if(text.substring(j, j+1) == key) //jika isi sms sesuai dengan karakter yg digunakan untuk pemisah('#') maka...
    {
      valSplit[countSplitSecond] = text.substring(lastIndexSecond,j);//simpan kedalam variabel valSpilt[x],
      lastIndexSecond = j + 1;
      Serial.print(countSplitSecond);
      Serial.print(":");
      Serial.println(valSplit[countSplitSecond]);
      countSplitSecond++;
    }
  }
}
// ### func untuk membaca waktu
void readTime()
{
  DateTime now = rtc.now();
  tahun = now.year();
  bulan = now.month();
  tanggal = now.day();
  jam = now.hour();
  menit = now.minute();
  detik = now.second();

  Serial.println();
  Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
  Serial.print(","); Serial.print(tanggal); Serial.print("/");Serial.print(bulan); Serial.print("/");Serial.print(tahun); 
  Serial.print(" "); Serial.print(jam); Serial.print(":");Serial.print(menit); Serial.print(":");Serial.println(detik); 
  
}
//### func untuk membaca suhu 
void readTemp()
{
  temp = thermistor.read();
  temp = temp/10;
  Serial.print("Temperature: "); Serial.print(temp); Serial.println(" C");
}

//## func utnuk membaca ph sensor
void readPH()
{
  static unsigned long samplingTime = millis();
  static unsigned long printTime = millis();
  static float voltage;
  if(millis()-samplingTime > samplingInterval)
  {
      pHArray[pHArrayIndex++]=analogRead(SensorPin);
      if(pHArrayIndex==ArrayLenth)pHArrayIndex=0;
      voltage = avergearray(pHArray, ArrayLenth)*5.0/1024;
      pHValue = 3.5*voltage-Offset;
      samplingTime=millis();
  }
  if(millis() - printTime > printInterval)   //Every 800 milliseconds, print a numerical, convert the state of the LED indicator
  {
  Serial.print("Voltage:");
        Serial.print(voltage,2);
        Serial.print("    pH value: ");
  Serial.println(pHValue,2);
        printTime=millis();
  }
}
double avergearray(int* arr, int number){
  int i;
  int max,min;
  double avg;
  long amount=0;
  if(number<=0){
    Serial.println("Error number for the array to avraging!/n");
    return 0;
  }
  if(number<5){   //less than 5, calculated directly statistics
    for(i=0;i<number;i++){
      amount+=arr[i];
    }
    avg = amount/number;
    return avg;
  }else{
    if(arr[0]<arr[1]){
      min = arr[0];max=arr[1];
    }
    else{
      min=arr[1];max=arr[0];
    }
    for(i=2;i<number;i++){
      if(arr[i]<min){
        amount+=min;        //arr<min
        min=arr[i];
      }else {
        if(arr[i]>max){
          amount+=max;    //arr>max
          max=arr[i];
        }else{
          amount+=arr[i]; //min<=arr<=max
        }
      }//if
    }//for
    avg = (double)amount/(number-2);
  }//if
  return avg;
}
