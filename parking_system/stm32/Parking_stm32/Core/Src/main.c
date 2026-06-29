/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Parking System — STM32 I2C Slave, that cycles through 4 vehicles
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#pragma pack(push, 1)
typedef struct {
    char type;            /* 'S' or 'E'          [0]     */
    char customer_id[15]; /* "CAR-001\0"          [1-15]  */
    char latitude[8];     /* "32.0853\0"          [16-23] */
    char longitude[8];    /* "34.7817\0"          [24-31] */
    char city[16];        /* "TelAviv\0"          [32-47]  */
} gps_frame_t; /* total = 48 bytes           */
#pragma pack(pop)

/* Vehicle entry — one row per simulated car */
typedef struct {
    const char *customer_id;
    const char *latitude;
    const char *longitude;
    const char *city;
} vehicle_t;

/* ── App state ───────────────────────────────────── */
typedef enum {
    STATE_PARKING_START,
    STATE_PARKING_ACTIVE,
    STATE_PARKING_END,
	STATE_PARKING_WAIT
} app_state_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define STM32_SLAVE_ADDR   0x08     /* our own slave address */
#define FRAME_SIZE     48
#define PARK_DURATION_SEC   10      /* seconds car is parked   */
#define WAIT_BETWEEN_SEC    5       /* seconds between sessions */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

I2C_HandleTypeDef hi2c2;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/* ── Vehicle table — add more rows to simulate more cars ── */
static const vehicle_t g_vehicles[] = {
    { "CAR-001", "32.0853", "34.7817", "TelAviv"   },
    { "CAR-002", "31.7683", "35.2137", "Jerusalem" },
    { "CAR-003", "32.7940", "34.9896", "Haifa"     },
    { "CAR-004", "31.2518", "34.7913", "BeerSheva" },
};

#define NUM_VEHICLES (sizeof(g_vehicles) / sizeof(g_vehicles[0]))

static gps_frame_t  g_frame;         /* frame ready to send to BBG  */
static app_state_t g_park_state      = STATE_PARKING_START;
static uint32_t    g_sec_count  = 0;
static uint32_t         g_vehicle_idx = 0;   	   /* which vehicle is parking now */
static volatile uint8_t g_tick = 0; 			   /* set by timer IRQ every 1 sec */
static volatile uint8_t g_tx_done = 0;             /* set when I2C TX to BBG completes */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_I2C2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Fill g_frame from the current vehicle and type byte.
 *        Uses snprintf — always null-terminates, no truncation warnings.
 */
static void build_frame(char type)
{
    const vehicle_t *v = &g_vehicles[g_vehicle_idx % NUM_VEHICLES];

    memset(&g_frame, 0, sizeof(g_frame));
    g_frame.type = type;
    snprintf(g_frame.customer_id, sizeof(g_frame.customer_id),
             "%s", v->customer_id);
    snprintf(g_frame.latitude,    sizeof(g_frame.latitude),
             "%s", v->latitude);
    snprintf(g_frame.longitude,   sizeof(g_frame.longitude),
             "%s", v->longitude);
    snprintf(g_frame.city,        sizeof(g_frame.city),
             "%s", v->city);
}

/**
 * @brief Arm the I2C slave for one transmit transaction.
 *        Called from main (never from IRQ).
 */
static void i2c_arm(void)
{
    HAL_I2C_Slave_Transmit_IT(&hi2c2,
                               (uint8_t *)&g_frame,
                               FRAME_SIZE);
}

/**
 * @brief I2C slave TX callback — called by HAL when master requests data.
 *        Sends the pre-prepared frame buffer.
 */
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C2) return;

    /* Re-arm immediately for the next read from BBG */
    g_tx_done = 1;              /* tell main loop a TX just completed */
    HAL_I2C_Slave_Transmit_IT(&hi2c2,
                               (uint8_t *)&g_frame,
                               FRAME_SIZE);
}

/**
 * @brief I2C error callback — re-arm the slave so it stays responsive.
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C2) return;

    /* Re-arm after any error (e.g. master sent NACK at end of read) */
    HAL_I2C_Slave_Transmit_IT(&hi2c2,
                               (uint8_t *)&g_frame,
                               FRAME_SIZE);
}

/* ── Timer callback: fires every 1 second ────────── */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;
    g_tick = 1;   /* signal main loop */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART3_UART_Init();
  MX_I2C2_Init();
  /* USER CODE BEGIN 2 */
  /* Prepare the first frame */
  build_frame('S');
  i2c_arm();              /* arm slave — ready for BBG to read    */

  /* Start 1-second timer */
  HAL_TIM_Base_Start_IT(&htim2);

  printf("=== STM32 I2C Slave started ===\r\n");
  printf("Slave addr : 0x%02X\r\n", STM32_SLAVE_ADDR);
  printf("Frame size : %d bytes\r\n", FRAME_SIZE);
  printf("Num vehicles : %d\r\n", (int)NUM_VEHICLES);
  printf("Waiting for BBG master...\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
      /* ── Handle completed TX (print outside IRQ) ── */
      if (g_tx_done) {
          g_tx_done = 0;
          printf("[TX] type=%c id=%s lat=%s lon=%s city=%s\r\n",
                 g_frame.type,
                 g_frame.customer_id,
                 g_frame.latitude,
                 g_frame.longitude,
                 g_frame.city);
      }

      /* Update parking state machine every second */
      if (!g_tick) continue;
      g_tick = 0;

	  switch (g_park_state) {

	  /* ── Send START, begin counting ─────────── */
      case STATE_PARKING_START:
    	  build_frame('S');
          i2c_arm();
          g_sec_count  = 0;
          g_park_state = STATE_PARKING_ACTIVE;
          printf("[STATE] STARTED vehicle=%s city=%s\r\n",
                 g_frame.customer_id, g_frame.city);
          break;

      /* ── Count parking seconds ───────────────── */
      case STATE_PARKING_ACTIVE:
    	  g_sec_count++;
    	  printf("[STATE] ACTIVE %s — %lu/%d sec\r\n",
				 g_frame.customer_id,
				 (unsigned long)g_sec_count,
				 PARK_DURATION_SEC);
           if (g_sec_count >= PARK_DURATION_SEC) {
               g_park_state = STATE_PARKING_END;
           }
          break;

      /* ── Switch frame to END ─────────────────── */
      case STATE_PARKING_END:
    	  build_frame('E');
          i2c_arm();
          g_sec_count  = 0;
          g_park_state = STATE_PARKING_WAIT;
          printf("[STATE] ENDED %s — waiting %d sec\r\n",
				 g_frame.customer_id, WAIT_BETWEEN_SEC);
          break;

	  /* ── Wait between sessions (no HAL_Delay) ── */
	  case STATE_PARKING_WAIT:
		  g_sec_count++;
		  if (g_sec_count >= WAIT_BETWEEN_SEC) {
			  g_sec_count  = 0;
			  g_vehicle_idx++;   /* advance to next vehicle */
			  g_park_state = STATE_PARKING_START;
			  printf("[STATE] Next vehicle: %s\r\n\n",
					 g_vehicles[g_vehicle_idx % NUM_VEHICLES].customer_id);
		  }
		  break;
	  }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00303D5B;
  hi2c2.Init.OwnAddress1 = 16;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 15999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
