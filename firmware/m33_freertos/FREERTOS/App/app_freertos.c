/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_azure_rtos.c
  * @author  MCD Application Team
  * @brief   app_azure_rtos application implementation file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
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
#include "app_freertos.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FW_NAME          "m33_freertos M3.5"
#define LED_PERIOD_MS    100U
#define RX_QUEUE_LEN     16U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

static osSemaphoreId_t ShutDownSemaphore;
static const osSemaphoreAttr_t semAttr_ShutDownSemaphore = {
    .name = "ShutDownSemaphore",
};
static osThreadId_t ShutDownThreadHandle;
static const osThreadAttr_t ShutDown_attributes = {
  .name = "ShutDown Thread",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 2 * 1024
};

static osThreadId_t led_thread;
static const osThreadAttr_t led_thread_attr = {
  .name = "led",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512
};

/* printf (newlib nano) needs more than the 512 bytes the example used */
static osThreadId_t console_thread;
static const osThreadAttr_t console_thread_attr = {
  .name = "console",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 1024
};

static osMessageQueueId_t rx_queue;
static uint8_t rx_byte;
static volatile uint32_t led_toggles;

static IPCC_HandleTypeDef const *hipcc_handle;
static uint32_t ipcc_ch_id;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void led_thread_entry(void *argument);
static void console_thread_entry(void *argument);
static void console_rx_start(void);
static void console_rx_done(UART_HandleTypeDef *huart);
static void console_rx_error(UART_HandleTypeDef *huart);
static void MX_FREERTOS_DeInit(void);
static void shutdown_thread_entry(void *argument);
/* USER CODE END PFP */

static void MX_FREERTOS_DeInit(void)
{
  HAL_NVIC_DisableIRQ(UART5_IRQn);
  (void) HAL_UART_AbortReceive(&hcom_uart[COM_VCP_CM33]);
  HAL_DeInitTick();
  (void) osSemaphoreDelete(ShutDownSemaphore);
  (void) osThreadTerminate(led_thread);
  (void) osThreadTerminate(console_thread);
  (void) osMessageQueueDelete(rx_queue);
}

static void shutdown_thread_entry(void *argument)
{
  osSemaphoreAcquire(ShutDownSemaphore, osWaitForever);

  printf("[m33] shutdown tick=%lu led_toggles=%lu\r\n",
         (unsigned long) osKernelGetTickCount(), (unsigned long) led_toggles);

  MX_FREERTOS_DeInit();

  /* DeInitialize the LED3 */
  BSP_LED_DeInit(LED3);

  /* When ready, notify the remote processor that we can be shut down */
  HAL_IPCC_NotifyCPU(hipcc_handle, ipcc_ch_id, IPCC_CHANNEL_DIR_RX);

  /* Wait for complete shutdown */
  while(1);
}

/**
  * @brief  Callback from IPCC Interrupt Handler: Remote Processor asks local processor to shutdown
  * @param  hipcc IPCC handle
  * @param  ChannelIndex Channel number
  * @param  ChannelDir Channel direction
  * @retval None
  */
void CoproSync_ShutdownCb(IPCC_HandleTypeDef * hipcc, uint32_t ChannelIndex, IPCC_CHANNELDirTypeDef ChannelDir)
{
  hipcc_handle = hipcc;
  ipcc_ch_id = ChannelIndex;
  (void) osSemaphoreRelease(ShutDownSemaphore);
}
  /**
  * @brief  Define the initial system.
  * @param  first_unused_memory : Pointer to the first unused memory
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  ShutDownSemaphore = osSemaphoreNew(1, 0, &semAttr_ShutDownSemaphore);
   if (NULL == ShutDownSemaphore)
   {
   Error_Handler();
   }
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  rx_queue = osMessageQueueNew(RX_QUEUE_LEN, sizeof(uint8_t), NULL);
  if (NULL == rx_queue)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_QUEUES */
  /* creation of Task */
  ShutDownThreadHandle = osThreadNew(shutdown_thread_entry, NULL, &ShutDown_attributes);
  if (NULL == ShutDownThreadHandle)
  {
    Error_Handler();
  }

  led_thread = osThreadNew(led_thread_entry, NULL, &led_thread_attr);
  if (NULL == led_thread)
  {
    Error_Handler();
  }

  console_thread = osThreadNew(console_thread_entry, NULL, &console_thread_attr);
  if (NULL == console_thread)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/**
  * @brief  Toggle LED3 every LED_PERIOD_MS, drift-free (absolute wake-up time)
  * @param  argument not used
  * @retval None
  */
static void led_thread_entry(void *argument)
{
  uint32_t next = osKernelGetTickCount();
  (void) argument;

  for (;;)
  {
    BSP_LED_Toggle(LED3);
    led_toggles++;
    next += LED_PERIOD_MS;
    (void) osDelayUntil(next);
  }
}

static void console_print_info(void)
{
  uint32_t hal = HAL_GetHalVersion();
  uint32_t cpuid = SCB->CPUID;

  printf("\r\n[m33] %s build=%s %s\r\n", FW_NAME, __DATE__, __TIME__);
  printf("[m33] cpuid=0x%08lx r%lup%lu %s\r\n", (unsigned long) cpuid,
         (unsigned long) ((cpuid & SCB_CPUID_VARIANT_Msk) >> SCB_CPUID_VARIANT_Pos),
         (unsigned long) (cpuid & SCB_CPUID_REVISION_Msk),
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
         "secure"
#else
         "non-secure"
#endif
         );
  printf("[m33] core_clock=%lu Hz\r\n", (unsigned long) SystemCoreClock);
  printf("[m33] bootsr=0x%08lx developer_boot=%d\r\n",
         (unsigned long) SYSCFG->BOOTSR, IS_DEVELOPER_BOOT_MODE() ? 1 : 0);
  /* DBGMCU sits on the debug APB, only clocked when RCC_DBGCFGR.DBGEN is set */
  if ((RCC->DBGCFGR & RCC_DBGCFGR_DBGEN) != 0U)
  {
    printf("[m33] dev_id=0x%03lx rev_id=0x%04lx\r\n",
           (unsigned long) HAL_GetDEVID(), (unsigned long) HAL_GetREVID());
  }
  else
  {
    printf("[m33] dev_id=? rev_id=? (debug clock off)\r\n");
  }
  printf("[m33] hal=%lu.%lu.%lu freertos=%s tick=%lu Hz heap_free=%lu/%lu\r\n",
         (unsigned long) (hal >> 24), (unsigned long) ((hal >> 16) & 0xFFU),
         (unsigned long) ((hal >> 8) & 0xFFU), tskKERNEL_VERSION_NUMBER,
         (unsigned long) osKernelGetTickFreq(), (unsigned long) xPortGetFreeHeapSize(),
         (unsigned long) configTOTAL_HEAP_SIZE);
  printf("[m33] running; led3 period=%lu ms, any key for uptime\r\n", (unsigned long) LED_PERIOD_MS);
}

static void console_print_uptime(uint8_t ch)
{
  uint32_t ms = osKernelGetTickCount();
  uint32_t s = ms / 1000U;

  printf("[m33] key=0x%02x tick=%lu uptime=%02lu:%02lu:%02lu.%03lu led_toggles=%lu stack_free led=%lu console=%lu\r\n",
         ch, (unsigned long) ms,
         (unsigned long) (s / 3600U), (unsigned long) ((s / 60U) % 60U),
         (unsigned long) (s % 60U), (unsigned long) (ms % 1000U),
         (unsigned long) led_toggles,
         (unsigned long) osThreadGetStackSpace(led_thread),
         (unsigned long) osThreadGetStackSpace(console_thread));
}

/**
  * @brief  Print hardware info, then print the uptime for each received byte
  * @param  argument not used
  * @retval None
  */
static void console_thread_entry(void *argument)
{
  uint8_t ch;
  (void) argument;

  console_print_info();
  console_rx_start();

  for (;;)
  {
    if (osMessageQueueGet(rx_queue, &ch, NULL, osWaitForever) == osOK)
    {
      console_print_uptime(ch);
    }
  }
}

static void console_rx_start(void)
{
  UART_HandleTypeDef *huart = &hcom_uart[COM_VCP_CM33];

  if ((HAL_UART_RegisterCallback(huart, HAL_UART_RX_COMPLETE_CB_ID, console_rx_done) != HAL_OK) ||
      (HAL_UART_RegisterCallback(huart, HAL_UART_ERROR_CB_ID, console_rx_error) != HAL_OK))
  {
    Error_Handler();
  }
  HAL_NVIC_SetPriority(UART5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + DEFAULT_IRQ_PRIO, 0);
  HAL_NVIC_EnableIRQ(UART5_IRQn);
  if (HAL_UART_Receive_IT(huart, &rx_byte, 1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ISR context: hand the byte to the console thread and re-arm */
static void console_rx_done(UART_HandleTypeDef *huart)
{
  (void) osMessageQueuePut(rx_queue, &rx_byte, 0U, 0U);
  (void) HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

/* overrun or framing error aborts the reception, so re-arm it */
static void console_rx_error(UART_HandleTypeDef *huart)
{
  (void) HAL_UART_Receive_IT(huart, &rx_byte, 1);
}
