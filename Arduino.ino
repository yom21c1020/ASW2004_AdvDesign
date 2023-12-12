#include <Stepper.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const int stepsPerRevolution = 200;

//SteppMotor를 위한 Pin
#define IN1 19
#define IN2 18
#define IN3 5
#define IN4 17

//UltraSonic(초음파)를 위한 Pin
#define TRIGPIN 25
#define ECHOPIN 26

//부저(스피커)알림을 위한 Pin
#define BUZZER_PIN 32
int noteDurations[] = {4, 8, 8, 4, 4, 4, 4, 4}; //각 음계(음)별 소리를 얼마동안 길게 울리게 할지 길이를 정해둔 배열
int melody[] = { 262, 196, 196, 220, 196, 0, 247, 262 }; //음계를 모아둔 배열

//LCD를 사용하기 위한 객체선언
LiquidCrystal_I2C lcd(0x27, 16, 2); 

//약이 몇 개 남았는지 확인하기 위한 변수
int count = 0; //최대 8칸이니 count가 8번이 되면 다 떨어졌다고 반환

Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

void setup() {
    //시리얼 통신 속도 설정, 라즈베리파이의 파이썬코드상 속도와 일치할 필요
    Serial.begin(9600);
    
    //초음파 센서를 위한 핀번호 설정
    pinMode(TRIGPIN, OUTPUT);
    pinMode(ECHOPIN, INPUT);
    
    //스텝모터 속도 설정
    myStepper.setSpeed(60);
    
    //LCD화면출력
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0,0);
    lcd.print("Initializing...");
    delay(600);
    lcd.clear();
}


// 라즈베리파이로부터 controlSteppMotor라는 문자열을 parsing하면 controlSteppMotor 함수 실행
void controlSteppMotor(int stepsPerRevolution) {
    myStepper.step(stepsPerRevolution);
  
    //모터가 정상적으로 되면 'SteppMotorSuccess' + '\n' 을 시리얼에 출력하여
    //라즈베리파이가 SteppMotorSuccess라는 문자열을 읽도록 함
    Serial.println("SteppMotorSuccess"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
}

// '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용" 
void ultrasonicSensor() {
    double duration;
    double distance;
  
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);
  
    duration = pulseIn(ECHOPIN, HIGH);
    distance = duration * 17 / 1000;
  
    //닫힌 상태 => 초음파 센서가 단거리를 잘 인식하지 못해서 (가까지 가져가도 20cm로 나오는 경우도 많음)
    //추후 조립시에는 센서가 거리를 잘 판단할 수 있도록 해야
    if (distance < 30) {  //30cm 미만이면 닫힘 상태로 (추후 조립해보고 필요에 따라 값 변경 필요)
        Serial.println("coverClosed"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
    }
    else {
        Serial.println("coverOpened"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
    }
}

//사용자가 약을 챙겨먹지 않으면 부저를 울리는 함수
void turnBuzzerOn() {
    for (int thisNote = 0; thisNote < 8; thisNote++) {
        int noteDuration = 1000 / noteDurations [thisNote];
        tone (BUZZER_PIN, melody[thisNote], noteDuration);
    
        int pauseBetweenNotes = noteDuration * 1.30;
        delay(pauseBetweenNotes);
        noTone(BUZZER_PIN);
    }
    
    Serial.println("buzzerSuccessfullyOn"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
}

//남은 알약 LCD표시기능
void showLCD() {
    if (count == 8) { //8번 제공했으면 다 떨어졌음을 알림
        Serial.println("runOutOfPills");
    }
    else if (count >= 0 || count < 9) {
        int left = 8 - count;
        lcd.setCursor(0, 0);
        lcd.print("Remaining Pills");
        lcd.setCursor(0,1);
        lcd.print(": " + String(left));
        Serial.println("lcdSuccess"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
        delay(5000);
        lcd.clear();
    
        count = count + 1;
    }
    else {
        Serial.println("ErrorNeedRefill"); // 8번 이상 제공한 경우 에러를 반환 => 해당 에러가 나오면 라즈베리파이에서 setRefill()함수를 호출
    }
}

//알약 refill 기능
void refillPills() {
    count = 0; //showLCD()에 있는 count 변수 0으로 초기화
    Serial.println("refillSuccess"); // '\n'을 구분으로 시리얼 메시지를 읽어옴으로 "결과를 반환하려는 순간만 println을 사용"
}


void loop() {
    //시리얼이 정상이면
    if (Serial.available() > 0) {
        //라즈베리파이로부터 '\n'이 들어오기 전까지 시리얼 메시지를 읽어옴
        String data = Serial.readStringUntil('\n');
        //라즈베리파이에서 controlSteppMotor라는 문자열을 parsing하면 controlSteppMotor 함수 실행
        if (data.equals("controlSteppMotor")) {
          controlSteppMotor(stepsPerRevolution);
        }
        //라즈베리파이에서 checkCover라는 문자열을 parsing하면 ultrasonicSensor 함수 실행
        else if (data.equals("checkCover")) {
          ultrasonicSensor();
        }
        //라즈베리파이에서 buzzerOn이라는 문자열을 parsing하면 turnBuzzerOn 함수 실행
        else if (data.equals("buzzerOn")) {
          turnBuzzerOn();
        }
        //라즈베리파이에서 lcdOn이라는 문자열을 parsing하면 showLCD 함수 실행
        else if (data.equals("lcdON")) {
          showLCD();
        }
        //라즈베리파이에서 refill라는 문자열을 parsing하면 refillPills 함수 실행
        else if (data.equals("refill")) {
          refillPills();
        }
    }
}