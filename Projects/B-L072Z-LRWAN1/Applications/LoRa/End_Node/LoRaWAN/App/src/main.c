/**
  ******************************************************************************
  * @file    main.c
  * @author  MCD Application Team
  * @brief   this is the main!
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2018 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "hw.h"
#include "low_power_manager.h"
#include "lora.h"
#include "bsp.h"
#include "timeServer.h"
#include "vcom.h"
#include "version.h"
#include "sensor.h"
#include "sound_classifier.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

#define LORAWAN_MAX_BAT 254
#define LPP_APP_PORT    99
#define MAG_LPF_FACTOR  0.4f
#define ACC_LPF_FACTOR  0.1f

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*!
 * Defines the application data transmission duty cycle in minutes
 */
#define APP_TX_DUTYCYCLE                            3
/*!
 * LoRaWAN Adaptive Data Rate
 * @note Please note that when ADR is enabled the end-device should be static
 */
#define LORAWAN_ADR_STATE                           LORAWAN_ADR_ON
/*!
 * LoRaWAN Default data Rate Data Rate
 * @note Please note that LORAWAN_DEFAULT_DATA_RATE is used only when ADR is disabled
 */
#define LORAWAN_DEFAULT_DATA_RATE                   DR_3
/*!
 * LoRaWAN application port
 * @note do not use 224. It is reserved for certification
 */
#define LORAWAN_APP_PORT                            2
/*!
 * LoRaWAN default endNode class port
 */
#define LORAWAN_DEFAULT_CLASS                       CLASS_A
/*!
 * LoRaWAN default confirm state
 */
#define LORAWAN_DEFAULT_CONFIRM_MSG_STATE           LORAWAN_UNCONFIRMED_MSG
/*!
 * User application data buffer size
 */
#define LORAWAN_APP_DATA_BUFF_SIZE                  64
/*!
 * Defines the frequency sub-band of the network server to connect to
 *        1: Everynet/Kore
 *        2: TTN
 */
#define LORAWAN_FSB                                 2

/*!
 * User application data
 */
static uint8_t AppDataBuff[LORAWAN_APP_DATA_BUFF_SIZE];

/*!
 * User application data structure
 */
lora_AppData_t AppData = { AppDataBuff,  0, 0 };

/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/* prints the payload to be sent in hex format */
static void PrintHexBuffer( uint8_t *buffer, uint8_t size );

/* call back when LoRa endNode has received a frame*/
static void LORA_RxData(lora_AppData_t *AppData);

/* call back when LoRa endNode has just joined*/
static void LORA_HasJoined(void);

/* call back when LoRa endNode has just switch the class*/
static void LORA_ConfirmClass(DeviceClass_t Class);

/* call back when server needs endNode to send a frame*/
static void LORA_TxNeeded(void);

/* calculate heading from magneto value*/
static void ConvertGaussToDegree(sensor_t *sensor_data);

/* callback to get the battery level in % of full charge (254 full charge, 0 no charge)*/
static uint8_t LORA_GetBatteryLevel(void);

/* LoRa endNode send request*/
static void Send(void *context);

/* LoRa endNode send request*/
static void send_sound_classifier(void *context);

/* start the tx process*/
static void LoraStartTx(TxEventType_t EventType);

/* tx timer callback function*/
static void OnTxTimerEvent(void *context);

/* tx timer callback function*/
static void LoraMacProcessNotify(void);

/* Private variables ---------------------------------------------------------*/
/* load Main call backs structure*/
static LoRaMainCallback_t LoRaMainCallbacks = { LORA_GetBatteryLevel,
                                                HW_GetTemperatureLevel,
                                                HW_GetUniqueId,
                                                HW_GetRandomSeed,
                                                LORA_RxData,
                                                LORA_HasJoined,
                                                LORA_ConfirmClass,
                                                LORA_TxNeeded,
                                                LoraMacProcessNotify
                                              };
LoraFlagStatus LoraMacProcessRequest = LORA_RESET;
LoraFlagStatus AppProcessRequest = LORA_RESET;
/*!
 * Specifies the state of the application LED
 */
static uint8_t AppLedStateOn = RESET;

static TimerEvent_t TxTimer;

#ifdef USE_B_L072Z_LRWAN1
/*!
 * Timer to handle the application Tx Led to toggle
 */
static TimerEvent_t TxLedTimer;
static void OnTimerLedEvent(void *context);

#endif

float heading;
SensorAxesRaw_t MAG_MIN = { 0XFE64, 0XE43B, 0 };
SensorAxesRaw_t MAG_MAX = { 0X1987, 0, 0X1848 };
SensorAxesRaw_t MAGNETO_Value_Old   = { 0, 0, 0 };
SensorAxesRaw_t ACCELERO_Value_Old  = { 0, 0, 0 };

/*!
 * Controls the number of transmissions when device connects
 */
uint8_t upCnt = 0;

/* !
 *Initialises the Lora Parameters
 */
static  LoRaParam_t LoRaParamInit = {LORAWAN_ADR_STATE,
                                     LORAWAN_DEFAULT_DATA_RATE,
                                     LORAWAN_PUBLIC_NETWORK,
                                     LORAWAN_FSB
                                    };

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
  /* STM32 HAL library initialization*/
  HAL_Init();

  /* Configure the system clock*/
  SystemClock_Config();

  /* Configure the debug mode*/
  DBG_Init();

  /* Configure the hardware*/
  HW_Init();

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /*Disbale Stand-by mode*/
  LPM_SetOffMode(LPM_APPLI_Id, LPM_Disable);

  /* Configure the Lora Stack*/
  LORA_Init(&LoRaMainCallbacks, &LoRaParamInit);

  LORA_Join();

  LoraStartTx(TX_ON_TIMER);

  while (1)
  {
    if (AppProcessRequest == LORA_SET)
    {
      /*reset notification flag*/
      AppProcessRequest = LORA_RESET;
      /*Send*/
      send_sound_classifier(NULL);
    }
    if (LoraMacProcessRequest == LORA_SET)
    {
      /*reset notification flag*/
      LoraMacProcessRequest = LORA_RESET;
      LoRaMacProcess();
    }
    /*If a flag is set at this point, mcu must not enter low power and must loop*/
    DISABLE_IRQ();

    /* if an interrupt has occurred after DISABLE_IRQ, it is kept pending
     * and cortex will not enter low power anyway  */
    if ((LoraMacProcessRequest != LORA_SET) && (AppProcessRequest != LORA_SET))
    {
#ifndef LOW_POWER_DISABLE
      LPM_EnterLowPower();
#endif
    }

    ENABLE_IRQ();

    /* USER CODE BEGIN 2 */
    /* USER CODE END 2 */
  }
}


void LoraMacProcessNotify(void)
{
  LoraMacProcessRequest = LORA_SET;
}

static void LORA_HasJoined(void)
{
#if( OVER_THE_AIR_ACTIVATION != 0 )
  PRINTF(" JOIN ACCEPTED\n\r");
#endif
  LORA_RequestClass(LORAWAN_DEFAULT_CLASS);
  
  MibRequestConfirm_t mibReq;
  mibReq.Type = MIB_CHANNELS_DATARATE;
  mibReq.Param.ChannelsDatarate = LORAWAN_DEFAULT_DATA_RATE;
  LoRaMacMibSetRequestConfirm( &mibReq );

  /* first uplink right after join accept*/
  AppProcessRequest = LORA_SET;
}

static void ConvertGaussToDegree(sensor_t *sensor_data)
{  
  heading = ( 180.0 * atan2( sensor_data->magneto.AXIS_Y , sensor_data->magneto.AXIS_X  ) / M_PI );

  if(heading < 0) 
  {
    heading += 360;
  }
}

static void Send(void *context)
{
  /* USER CODE BEGIN 3 */
  uint16_t pressure = 0;
  int16_t temperature = 0;
  uint16_t humidity = 0;
  int16_t   magneto = 0;
  SensorAxesRaw_t accelero  = { 0, 0, 0 };
  SensorAxesRaw_t gyro      = { 0, 0, 0 };
  uint8_t batteryLevel;
  sensor_t sensor_data;

  if (LORA_JoinStatus() != LORA_SET)
  {
    /*Not joined, try again later*/
    LORA_Join();
    return;
  }

  PRINTF("\n########################\n\r");
  PRINTF("SENSORS READING\n\r");

#ifdef USE_B_L072Z_LRWAN1
  TimerInit(&TxLedTimer, OnTimerLedEvent);

  TimerSetValue(&TxLedTimer, 200);

  LED_On(LED_RED1) ;

  TimerStart(&TxLedTimer);
#endif

  BSP_sensor_Read(&sensor_data);
  ConvertGaussToDegree(&sensor_data);

  PRINTF("Temperature: %.2f Celsius\n", sensor_data.temperature);
  PRINTF("Humidity: %.2f %%\n", sensor_data.humidity);
  PRINTF("Pressure: %.2f hPa\n", sensor_data.pressure);
  PRINTF("Accelerometer [x,y,z]: [%.2f g, %.2f g, %.2f g]\n", sensor_data.accelero.AXIS_X/1000.0, sensor_data.accelero.AXIS_Y/1000.0, sensor_data.accelero.AXIS_Z/1000.0);
  PRINTF("Gyroscope [x,y,z]: [%.2f dps, %.2f dps, %.2f dps]\n", sensor_data.gyro.AXIS_X/1000.0, sensor_data.gyro.AXIS_Y/1000.0, sensor_data.gyro.AXIS_Z/1000.0);
  PRINTF("Magnetometer: %.2f degrees\n", heading);

  temperature       = (int16_t)(sensor_data.temperature * 100);         /* in �C * 100 */
  pressure          = (uint16_t)(sensor_data.pressure * 10);            /* in hPa * 10 */
  humidity          = (uint16_t)(sensor_data.humidity * 10);            /* in % *10    */
  accelero.AXIS_X   = (int16_t)( sensor_data.accelero.AXIS_X );
  accelero.AXIS_Y   = (int16_t)( sensor_data.accelero.AXIS_Y );
  accelero.AXIS_Z   = (int16_t)( sensor_data.accelero.AXIS_Z );
  gyro.AXIS_X       = (int16_t)( sensor_data.gyro.AXIS_X );
  gyro.AXIS_Y       = (int16_t)( sensor_data.gyro.AXIS_Y );
  gyro.AXIS_Z       = (int16_t)( sensor_data.gyro.AXIS_Z );
  magneto           = (int16_t)( heading * 100 );
  uint32_t i = 0;

  batteryLevel = LORA_GetBatteryLevel(); /* 1 (very low) to 254 (fully charged) */

  AppData.Port = LORAWAN_APP_PORT;
  // PRESSURE SENSOR
  AppData.Buff[i++] = (pressure >> 8) & 0xFF;
  AppData.Buff[i++] = pressure & 0xFF;
  // TEMPERATURE SENSOR
  AppData.Buff[i++] = (temperature >> 8) & 0xFF;
  AppData.Buff[i++] = temperature & 0xFF;
  // HUMIDITY SENSOR
  AppData.Buff[i++] = (humidity >> 8) & 0xFF;
  AppData.Buff[i++] = humidity & 0xFF;
    // ACCELERO SENSOR
  AppData.Buff[i++] = (accelero.AXIS_X >> 8) & 0xFF;
  AppData.Buff[i++] = accelero.AXIS_X & 0xFF;
  AppData.Buff[i++] = (accelero.AXIS_Y >> 8) & 0xFF;
  AppData.Buff[i++] = accelero.AXIS_Y & 0xFF;
  AppData.Buff[i++] = (accelero.AXIS_Z >> 8) & 0xFF;
  AppData.Buff[i++] = accelero.AXIS_Z & 0xFF;
  // GYROSCOPE SENSOR
  AppData.Buff[i++] = (gyro.AXIS_X >> 8) & 0xFF;
  AppData.Buff[i++] = gyro.AXIS_X & 0xFF;
  AppData.Buff[i++] = (gyro.AXIS_Y >> 8) & 0xFF;
  AppData.Buff[i++] = gyro.AXIS_Y & 0xFF;
  AppData.Buff[i++] = (gyro.AXIS_Z >> 8) & 0xFF;
  AppData.Buff[i++] = gyro.AXIS_Z & 0xFF;
  // ANALOG MAGNETO SENSOR : send value range -180 to 180 degree, North is zero degree.
  AppData.Buff[i++] = (magneto >> 8) & 0xFF;
  AppData.Buff[i++] = magneto & 0xFF;

  AppData.Buff[i++] = batteryLevel;
  AppData.BuffSize = i;

  PrintHexBuffer(AppData.Buff, AppData.BuffSize);
   
  LORA_send(&AppData, LORAWAN_DEFAULT_CONFIRM_MSG_STATE);

  /* USER CODE END 3 */
}

static void send_sound_classifier(void *context){
   /* USER CODE BEGIN 3 */
  uint16_t latitude;
  uint16_t longitude;
  uint8_t class;
  float_t tmp;
  classifier_data data;
  uint8_t batteryLevel;

  if (LORA_JoinStatus() != LORA_SET)
  {
    /*Not joined, try again later*/
    LORA_Join();
    return;
  }

  PRINTF("\n########################\n\r");
  PRINTF("CLASSIFIER READING\n\r");

#ifdef USE_B_L072Z_LRWAN1
  TimerInit(&TxLedTimer, OnTimerLedEvent);

  TimerSetValue(&TxLedTimer, 200);

  LED_On(LED_RED1) ;

  TimerStart(&TxLedTimer);
#endif

  read_gpio_sound_classifier(&data);

  PRINTF("Latitude: %d\n", data.latitude);
  PRINTF("Longitude: %d\n", data.longitude);
  PRINTF("Class: %d\n", data.class);
  
  latitude       = data.latitude;    
  longitude = data.longitude; 
  class   = data.class;
  batteryLevel = LORA_GetBatteryLevel(); /* 1 (very low) to 254 (fully charged) */

 
  uint32_t i = 0;

  AppData.Port = LORAWAN_APP_PORT;
  // Latitude
  AppData.Buff[i++] = (latitude >> 8) & 0xFF;
  AppData.Buff[i++] = latitude & 0xFF;
  // Longitude
  AppData.Buff[i++] = (longitude >> 8) & 0xFF;
  AppData.Buff[i++] = longitude & 0xFF;
  // Class
  AppData.Buff[i++] = class;
  //Batery level
  AppData.Buff[i++] = batteryLevel;

  AppData.BuffSize = i;

  PrintHexBuffer(AppData.Buff, AppData.BuffSize);
   
  LORA_send(&AppData, LORAWAN_DEFAULT_CONFIRM_MSG_STATE);

}

static void PrintHexBuffer( uint8_t *buffer, uint8_t size )
{
    PRINTF("PAYLOAD: ");

    for( uint8_t i = 0; i < size; i++ )
    {
        PRINTF( "%02X ", buffer[i] );
    }
    PRINTF( "\r\n\n" );
}


static void LORA_RxData(lora_AppData_t *AppData)
{
  /* USER CODE BEGIN 4 */
  PRINTF("PACKET RECEIVED ON PORT %d\n\r", AppData->Port);

  switch (AppData->Port)
  {
    case 3:
      /*this port switches the class*/
      if (AppData->BuffSize == 1)
      {
        switch (AppData->Buff[0])
        {
          case 0:
          {
            LORA_RequestClass(CLASS_A);
            break;
          }
          case 1:
          {
            LORA_RequestClass(CLASS_B);
            break;
          }
          case 2:
          {
            LORA_RequestClass(CLASS_C);
            break;
          }
          default:
            break;
        }
      }
      break;
    case LORAWAN_APP_PORT:
      if (AppData->BuffSize == 1)
      {
        AppLedStateOn = AppData->Buff[0] & 0x01;
        if (AppLedStateOn == RESET)
        {
          PRINTF("LED OFF\n\r");
          LED_Off(LED_BLUE) ;
        }
        else
        {
          PRINTF("LED ON\n\r");
          LED_On(LED_BLUE) ;
        }
      }
      break;
    case LPP_APP_PORT:
    {
      AppLedStateOn = (AppData->Buff[2] == 100) ?  0x01 : 0x00;
      if (AppLedStateOn == RESET)
      {
        PRINTF("LED OFF\n\r");
        LED_Off(LED_BLUE) ;

      }
      else
      {
        PRINTF("LED ON\n\r");
        LED_On(LED_BLUE) ;
      }
      break;
    }
    default:
      break;
  }
  /* USER CODE END 4 */
}

static void OnTxTimerEvent(void *context)
{
  if(LORAWAN_FSB == 1) /* Everynet/Kore */
  {
    if(upCnt < 5)
    {
      upCnt++;
    }
    else /* After 5 first transmissions, duty is configured to 1 hour */
    {
      TimerInit(&TxTimer, OnTxTimerEvent);    
      TimerSetValue(&TxTimer, 60*60000); /* duty cycle is configured in milliseconds */ 
    }
  }

  /*Wait for next tx slot*/
  TimerStart(&TxTimer);

  AppProcessRequest = LORA_SET;
}

static void LoraStartTx(TxEventType_t EventType)
{
  if (EventType == TX_ON_TIMER)
  {
    /* send everytime timer elapses */
    if(LORAWAN_FSB == 1) /* Everynet/Kore. Duty cycle starts with 1 minute */
    {
      TimerInit(&TxTimer, OnTxTimerEvent);    
      TimerSetValue(&TxTimer, 60000); /* duty cycle is configured in milliseconds */
      OnTxTimerEvent(NULL);
    } 
    else 
    { /* TTN. Duty cycle is defined by APP_TX_DUTYCYCLE variable */
      TimerInit(&TxTimer, OnTxTimerEvent);    
      TimerSetValue(&TxTimer, APP_TX_DUTYCYCLE*60000); /* duty cycle is configured in milliseconds */
      OnTxTimerEvent(NULL);
    }    
  }
  else
  {
    /* send everytime button is pushed */
    GPIO_InitTypeDef initStruct = {0};

    initStruct.Mode = GPIO_MODE_IT_RISING;
    initStruct.Pull = GPIO_PULLUP;
    initStruct.Speed = GPIO_SPEED_HIGH;

    HW_GPIO_Init(USER_BUTTON_GPIO_PORT, USER_BUTTON_PIN, &initStruct);
    HW_GPIO_SetIrq(USER_BUTTON_GPIO_PORT, USER_BUTTON_PIN, 0, Send);
  }
}

static void LORA_ConfirmClass(DeviceClass_t Class)
{
  PRINTF("switch to class %c done\n\r", "ABC"[Class]);

  /*Optionnal*/
  /*informs the server that switch has occurred ASAP*/
  AppData.BuffSize = 0;
  AppData.Port = LORAWAN_APP_PORT;

  LORA_send(&AppData, LORAWAN_UNCONFIRMED_MSG);
}

static void LORA_TxNeeded(void)
{
  AppData.BuffSize = 0;
  AppData.Port = LORAWAN_APP_PORT;

  LORA_send(&AppData, LORAWAN_UNCONFIRMED_MSG);
}

/**
  * @brief This function return the battery level
  * @param none
  * @retval the battery level  1 (very low) to 254 (fully charged)
  */
uint8_t LORA_GetBatteryLevel(void)
{
  uint16_t batteryLevelmV;
  uint8_t batteryLevel = 0;

  batteryLevelmV = HW_GetBatteryLevel();


  /* Convert batterey level from mV to linea scale: 1 (very low) to 254 (fully charged) */
  if (batteryLevelmV > VDD_BAT)
  {
    batteryLevel = LORAWAN_MAX_BAT;
  }
  else if (batteryLevelmV < VDD_MIN)
  {
    batteryLevel = 0;
  }
  else
  {
    batteryLevel = (((uint32_t)(batteryLevelmV - VDD_MIN) * LORAWAN_MAX_BAT) / (VDD_BAT - VDD_MIN));
  }

  return batteryLevel;
}

#ifdef USE_B_L072Z_LRWAN1
static void OnTimerLedEvent(void *context)
{
  LED_Off(LED_RED1) ;
}
#endif
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
