/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2021, Joris Dobbelsteen. */

#include <project.h>
#include <stdio.h>

#include "dsmr.h"
#include "common.h"
#include "meter.h"

void StackEventHandler(uint32 eventCode, void *eventParam);

enum BleIndications {
    BLE_INDICATIONS_POWER_CONSUMPTION = 0x01,
    BLE_INDICATIONS_POWER_TARIFF = 0x02,
    BLE_INDICATIONS_POWER_TIMESTAMP = 0x04,
    BLE_INDICATIONS_POWER_INSTANTANEOUSPOWER = 0x08,
    BLE_INDICATIONS_POWER_INSTANTANEOUSPHASEINFO = 0x10,
    BLE_INDICATIONS_GAS_CONSUMPTION = 0x20,
    BLE_INDICATIONS_GAS_TIMESTAMP = 0x40
};
static uint32 bleNotificationsEnabled = 0;
static uint32 blePasscode = 0;
static uint8 userFactoryReset = 0;

static uint32 CharacteristicToIndicationMask(CYBLE_GATT_DB_ATTR_HANDLE_T characteristic)
{
    switch (characteristic)
    {
    case CYBLE_POWER_METER_CONSUMPTION_CHAR_HANDLE:             return BLE_INDICATIONS_POWER_CONSUMPTION;
    case CYBLE_POWER_METER_TARIFF_CHAR_HANDLE:                  return BLE_INDICATIONS_POWER_TARIFF;
    case CYBLE_POWER_METER_TIMESTAMP_CHAR_HANDLE:               return BLE_INDICATIONS_POWER_TIMESTAMP;
    case CYBLE_POWER_METER_INSTANTANEOUS_POWER_CHAR_HANDLE:     return BLE_INDICATIONS_POWER_INSTANTANEOUSPOWER;
    case CYBLE_POWER_METER_INSTANTANEOUS_PHASEINFO_CHAR_HANDLE: return BLE_INDICATIONS_POWER_INSTANTANEOUSPHASEINFO;
    case CYBLE_GAS_METER_CONSUMPTION_CHAR_HANDLE:               return BLE_INDICATIONS_GAS_CONSUMPTION;
    case CYBLE_GAS_METER_TIMESTAMP_CHAR_HANDLE:                 return BLE_INDICATIONS_GAS_TIMESTAMP;
    default:                                                    return 0;
    }
}

static uint32 CccdToIndicationMask(CYBLE_GATT_DB_ATTR_HANDLE_T characteristic)
{
    switch (characteristic)
    {
    case CYBLE_POWER_METER_CONSUMPTION_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:             return BLE_INDICATIONS_POWER_CONSUMPTION;
    case CYBLE_POWER_METER_TARIFF_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:                  return BLE_INDICATIONS_POWER_TARIFF;
    case CYBLE_POWER_METER_TIMESTAMP_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:               return BLE_INDICATIONS_POWER_TIMESTAMP;
    case CYBLE_POWER_METER_INSTANTANEOUS_POWER_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:     return BLE_INDICATIONS_POWER_INSTANTANEOUSPOWER;
    case CYBLE_POWER_METER_INSTANTANEOUS_PHASEINFO_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE: return BLE_INDICATIONS_POWER_INSTANTANEOUSPHASEINFO;
    case CYBLE_GAS_METER_CONSUMPTION_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:               return BLE_INDICATIONS_GAS_CONSUMPTION;
    case CYBLE_GAS_METER_TIMESTAMP_CLIENT_CHARACTERISTIC_CONFIGURATION_DESC_HANDLE:                 return BLE_INDICATIONS_GAS_TIMESTAMP;
    default:                                                                                        return 0;
    }
}

void LowPower(void)
{
    if((CyBle_GetState() == CYBLE_STATE_ADVERTISING) ||
       (CyBle_GetState() == CYBLE_STATE_CONNECTED))
    {
        CyBle_EnterLPM(CYBLE_BLESS_DEEPSLEEP);
    }

    // Always do flash writes and wait for debug UART
    if (cyBle_pendingFlashWrite != 0
        || UART_Debug_SpiUartGetTxBufferSize() != 0 || UART_Debug_GET_TX_FIFO_SR_VALID != 0)
    {
        return;
    }
    
    /* No interrupts allowed while entering system low power modes */
    uint8 intStatus = CyEnterCriticalSection();
    
    // Check power states
    CYBLE_BLESS_STATE_T blessState = CyBle_GetBleSsState();
    enum METER_POWER_STATE_T appState = Meter_GetPowerState();
    
    if((blessState == CYBLE_BLESS_STATE_ECO_ON || blessState == CYBLE_BLESS_STATE_DEEPSLEEP)
        && appState == METER_POWER_STATE_DEEPSLEEP)
    {
        UART_Debug_Sleep();
        CySysPmDeepSleep(); /* System Deep-Sleep. 1.3uA mode */
        UART_Debug_Wakeup();
    }
    else if (blessState != CYBLE_BLESS_STATE_EVENT_CLOSE)
    {
        if (appState == METER_POWER_STATE_DEEPSLEEP)
        {
            /* Change HF clock source from IMO to ECO, as IMO can be stopped to save power as application doesn't need it */
            CySysClkWriteHfclkDirect(CY_SYS_CLK_HFCLK_ECO); 
            /* Stop IMO for reducing power consumption */
            CySysClkImoStop(); 
            /* Put the CPU to Sleep. 1.1mA mode */
            CySysPmSleep();
            /* Starts execution after waking up, start IMO */
            CySysClkImoStart();
            /* Change HF clock source back to IMO */
            CySysClkWriteHfclkDirect(CY_SYS_CLK_HFCLK_IMO);
        }
        else
        {
            /* Divide system clock, lowering core, but not peripheral clocks */
            CySysClkWriteSysclkDiv(CY_SYS_CLK_SYSCLK_DIV4);
            /* Put the CPU to Sleep. 1.1mA mode */
            CySysPmSleep();
            /* Change divider to improve performance */
            CySysClkWriteSysclkDiv(CY_SYS_CLK_SYSCLK_DIV1);
        }
    }
    CyExitCriticalSection(intStatus);
}

static void BleNotify(CYBLE_GATT_HANDLE_VALUE_PAIR_T* handle)
{
    if (bleNotificationsEnabled & CharacteristicToIndicationMask(handle->attrHandle))
    {
        // TODO retry needed due to exceeded stack buffer?
        CyBle_GattsNotification(cyBle_connHandle, handle);
        CyBle_ProcessEvents();
    }
}

void Meter_ReceivedHandler(struct dsmr_data_t* data)
{
    CYBLE_GATT_HANDLE_VALUE_PAIR_T handle;
    CyBle_ExitLPM();

    // Power meter
    
    handle.attrHandle = CYBLE_POWER_METER_CONSUMPTION_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->E_in[0]);
    handle.value.len = 4 * 2 * MAX_TARIFFS;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);

    handle.attrHandle = CYBLE_POWER_METER_TARIFF_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->tariff);
    handle.value.len = 1;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);
    
    handle.attrHandle = CYBLE_POWER_METER_TIMESTAMP_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->timestamp);
    handle.value.len = 8;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);
    
    // Power meter instantanous
    
    handle.attrHandle = CYBLE_POWER_METER_INSTANTANEOUS_POWER_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->P_in_total);
    handle.value.len = 12;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);

    handle.attrHandle = CYBLE_POWER_METER_INSTANTANEOUS_PHASEINFO_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->I[0]);
    handle.value.len = 4 * 4 * MAX_PHASES;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);
    
    // Gas meter
    
    handle.attrHandle = CYBLE_GAS_METER_CONSUMPTION_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->gas_in);
    handle.value.len = 4;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);

    handle.attrHandle = CYBLE_GAS_METER_TIMESTAMP_CHAR_HANDLE;
    handle.value.val = (uint8_t*)&(data->gas_timestamp);
    handle.value.len = 8;
    CyBle_GattsWriteAttributeValue(&handle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
    BleNotify(&handle);
}

void Ble_StoreState()
{
    if (cyBle_pendingFlashWrite != 0)
    {
        if (Meter_GetPowerState() == METER_POWER_STATE_DEEPSLEEP)
        {
            CYBLE_API_RESULT_T res = CyBle_StoreBondingData(0);
            printf("Save %d\n", res);
        }
    }
}

static uint32 Ble_ComputePasscode()
{
    // Die Y position (0x49000100, y_loc[7:0])
    // Die X position (0x49000101, x_loc[7:0])
    // Die Wafer Number (0x49000102, wafer_num[7:0])
    // Die Lot number LSB (0x49000103, lot_lsb[7:0])
    // Die Lot number MSB (0x49000104, lot_msb[7:0])
    // Die production WW (0x49000105, work_week[7:0])
    // Die production year and fab (0x49000106, year[3:0], fab[3:0])
    // Die Minor Revision number (0x49000107, minor[7:0])
    uint32 uniqueid[2];
    CyGetUniqueId(uniqueid);
    // 6-bytes: 00A050-XXXXXX (where last 3 bytes are based on silicon id)
    CYBLE_GAP_BD_ADDR_T addr;
    CyBle_GetDeviceAddress(&addr);
    //printf("Serial %08lx %08lx\n", uniqueid[0], uniqueid[1]);
    uint32 id = (uniqueid[0]) + (uniqueid[0] >> 27);
    id ^= (uniqueid[1] << 12) + (uniqueid[1] >> 1);
    id ^= ((uint32)addr.bdAddr[2]) << 16;
    id ^= ((uint32)addr.bdAddr[1]) << 8;
    id ^= ((uint32)addr.bdAddr[0]) * 7;
    uint32 id2 = id / 1000000;
    id += id2;
    id %= 1000000;
    return id;
}


static void PrintAddress(CYBLE_GAP_BD_ADDR_T* addr)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x\n", 
        addr->bdAddr[5], addr->bdAddr[4], addr->bdAddr[3],
        addr->bdAddr[2], addr->bdAddr[1], addr->bdAddr[0]);
}

static void PrintOwnAddress()
{
    printf("Address: ");
    CYBLE_GAP_BD_ADDR_T addr;
    CyBle_GetDeviceAddress(&addr);
    PrintAddress(&addr);
    printf("Passcode: %lu\n", blePasscode);
}

static void PrintDevices()
{
    printf("Bonded:\n");
    CYBLE_GAP_BONDED_DEV_ADDR_LIST_T list;
    CyBle_GapGetBondedDevicesList(&list);
    while (list.count != 0)
    {
        PrintAddress(list.bdAddrList + (--list.count));
    }
}

static void PerformFactoryReset()
{
    if (!userFactoryReset)
        return;
    
    // Clear bonded devices
    CYBLE_GAP_BONDED_DEV_ADDR_LIST_T list;
    CyBle_GapGetBondedDevicesList(&list);
    while (list.count != 0)
    {
        CyBle_GapRemoveBondedDevice(list.bdAddrList + (--list.count));
    }
    
    userFactoryReset = 0;
}

static int CheckFactoryReset()
{
    int factoryResetDelay = 5000; // ms
    const int blinkDelay = 100; // ms;
    
    uint8 save_led_ad = LED_Advertising_Read();
    uint8 save_led_dis = LED_Disconnect_Read();

    // Check for factory reset by having button pressed on startup for
    // about 5 seconds. We blink in this period (rapidly)
    while(!BTN_User_Read() && ((factoryResetDelay -= blinkDelay) > 0))
    {
        LED_Advertising_Write(!LED_Advertising_Read()); // Blink
        LED_Disconnect_Write(!LED_Disconnect_Read());   // Blink
        CyDelay(blinkDelay);
    }
    
    LED_Advertising_Write(save_led_ad);
    LED_Disconnect_Write(save_led_dis);
    
    return factoryResetDelay <= 0;
}

int main(void)
{
    // Enable all leds are on at startup
    
    CyGlobalIntEnable;   /* Enable global interrupts */

    UART_Debug_Start();
    UART_Debug_UartPutString("SmartMeter BLE by Joris Dobbelsteen\r\n");

    CyBle_Start(StackEventHandler);
    
    // Wait 100 ms to ensure LEDs are visible
    CyDelay(100);

    userFactoryReset = CheckFactoryReset();
    if (userFactoryReset != 0)
        UART_Debug_UartPutString("Performing factory reset\r\n");
    
    
    /* Wait for BLE Component to initialize */
    while(CyBle_GetState() == CYBLE_STATE_INITIALIZING)
    {
        CyBle_ProcessEvents();
    }
    
    Meter_SetReceivedDsmrHandler(Meter_ReceivedHandler);
    Meter_Start();
    
    PrintOwnAddress();
    PrintDevices();
    
    for(;;)
    {
        CyBle_ProcessEvents();
        Meter_ProcessEvents();
        Ble_StoreState();
        LowPower();
    }
}



void StackEventHandler(uint32 eventCode, void *eventParam)
{
    printf("BLE %lu\n", eventCode);
    (void)eventParam;
    switch(eventCode)
    {
        /* Generic events */

        case CYBLE_EVT_HOST_INVALID:
        break;

        case CYBLE_EVT_STACK_ON:
        {
            CYBLE_BLESS_CLK_CFG_PARAMS_T clockConfig;
            CyBle_GetBleClockCfgParam(&clockConfig);
            clockConfig.bleLlSca = CYBLE_LL_SCA_000_TO_020_PPM; // lowest power, using Cypress development boards
            CyBle_SetBleClockCfgParam(&clockConfig);
            PerformFactoryReset();                              // do factory reset is required
            blePasscode = Ble_ComputePasscode();
            CyBle_GapFixAuthPassKey(1, blePasscode);
            LED_Advertising_Write(LED_OFF);                     // turn led off when BLE is running
            LED_Disconnect_Write(LED_ON);                       // turn disconnect led on when BLE just reset
            CyBle_GappStartAdvertisement( CYBLE_ADVERTISING_FAST );
        }
        break;

        case CYBLE_EVT_TIMEOUT:
            printf("TIMEOUT\n");
        break;

        case CYBLE_EVT_HARDWARE_ERROR:
            printf("HARDWARE_ERROR\n");
        break;

        case CYBLE_EVT_HCI_STATUS:
            printf("HCI_STATUS\n");
        break;

        case CYBLE_EVT_STACK_BUSY_STATUS:
            printf("STACK_BUSY_STATUS\n");
        break;

        case CYBLE_EVT_PENDING_FLASH_WRITE:
            // Flash write already in variable cyBle_pendingFlashWrite
            printf("PENDING_FLASH_WRITE\n");
        break;


        /* GAP events */

        case CYBLE_EVT_GAP_AUTH_REQ:
            printf("GAP_AUTH_REQ\n");
        break;

        case CYBLE_EVT_GAP_PASSKEY_ENTRY_REQUEST:
            printf("GAP_PASSKEY_ENTRY_REQUEST\n");
        break;

        case CYBLE_EVT_GAP_PASSKEY_DISPLAY_REQUEST:
            printf("GAP_PASSKEY_DISPLAY_REQUEST\n");
        break;

        case CYBLE_EVT_GAP_AUTH_COMPLETE:
            printf("GAP_AUTH_COMPLETE\n");
        break;

        case CYBLE_EVT_GAP_AUTH_FAILED:
            printf("GAP_AUTH_FAILED\n");
            CyBle_GapDisconnect(cyBle_connHandle.bdHandle);
        break;

        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            // See CYBLE_EVT_GAP_ENHANCE_CONN_COMPLETE when link-layer privacy is enabled
            printf("GAP_DEVICE_CONNECTED\n");
            CyBle_GapAuthReq(cyBle_connHandle.bdHandle, &cyBle_authInfo);
            LED_Disconnect_Write(LED_OFF);
        break;

        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            printf("GAP_DEVICE_DISCONNECTED\n");
            LED_Disconnect_Write(LED_ON);
            bleNotificationsEnabled = 0;
            CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
        break;

        case CYBLE_EVT_GAP_ENCRYPT_CHANGE:
            printf("GAP_ENCRYPT_CHANGE\n");
        break;

        case CYBLE_EVT_GAP_CONNECTION_UPDATE_COMPLETE:
            printf("GAP_CONNECTION_UPDATE_COMPLETE\n");
        break;

        case CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT:
            printf("GAP_KEYINFO_EXCHNGE_CMPLT\n");
        break;

        case CYBLE_EVT_GAP_DATA_LENGTH_CHANGE:
            printf("GAP_DATA_LENGTH_CHANGE\n");
        break;
            
        case CYBLE_EVT_GAP_ENHANCE_CONN_COMPLETE:
            printf("GAP_ENHANCE_CONN_COMPLETE\n");
            CyBle_GapAuthReq(cyBle_connHandle.bdHandle, &cyBle_authInfo);
            LED_Disconnect_Write(LED_OFF);
        break;
            
        case CYBLE_EVT_GAP_SMP_NEGOTIATED_AUTH_INFO:
            printf("GAP_SMP_NEGOTIATED_AUTH_INFO\n");
        break;
            
        /* GAP Peripheral events */

        case CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP:
            printf("GAPP_ADVERTISEMENT_START_STOP\n");
        break;

        /* GATT events */

        case CYBLE_EVT_GATT_CONNECT_IND:
            printf("GATT_CONNECT_IND\n");
        break;

        case CYBLE_EVT_GATT_DISCONNECT_IND:
            printf("GATT_DISCONNECT_IND\n");
        break;

            
        /* GATT Server events (CYBLE_EVENT_T) */

        case CYBLE_EVT_GATTS_XCNHG_MTU_REQ:
            printf("GATTS_XCNHG_MTU_REQ\n");
        break;

        case CYBLE_EVT_GATTS_READ_CHAR_VAL_ACCESS_REQ:
        {
            CYBLE_GATTS_CHAR_VAL_READ_REQ_T* rdReq = (CYBLE_GATTS_CHAR_VAL_READ_REQ_T*)eventParam;
            printf("READ_CHAR_VAL_ACCESS_REQ 0x%x\n", rdReq->attrHandle);
        }
        break;    
            
        case CYBLE_EVT_GATTS_WRITE_REQ:
        case CYBLE_EVT_GATTS_WRITE_CMD_REQ:
        {
            CYBLE_GATTS_WRITE_REQ_PARAM_T* wrReqParam = (CYBLE_GATTS_WRITE_REQ_PARAM_T*)eventParam;
            printf("GATTS_WRITE_(CMD)_REQ %hu\n", wrReqParam->handleValPair.attrHandle);
            uint32 eventMask = CccdToIndicationMask(wrReqParam->handleValPair.attrHandle);
            if (eventMask) // Indication enable / disable
            {
                if (wrReqParam->handleValPair.value.val[0])
                    bleNotificationsEnabled |= eventMask;  // enable
                else
                    bleNotificationsEnabled &= !eventMask; // disable
                
				uint8 CCDValue[2] = {wrReqParam->handleValPair.value.val[0], 0};
                CYBLE_GATT_HANDLE_VALUE_PAIR_T  NotificationCCDHandle;
                NotificationCCDHandle.attrHandle = wrReqParam->handleValPair.attrHandle;
                NotificationCCDHandle.value.val = CCDValue;
                NotificationCCDHandle.value.len = 2;
                CyBle_GattsWriteAttributeValue(&NotificationCCDHandle, 0, &cyBle_connHandle, CYBLE_GATT_DB_LOCALLY_INITIATED);
            }
            
            if (eventCode == CYBLE_EVT_GATTS_WRITE_REQ)
            {
                CyBle_GattsWriteRsp(cyBle_connHandle);
            }
        }
        break;

            printf("GATTS_WRITE_CMD_REQ\n");
        break;

        case CYBLE_EVT_GATTS_PREP_WRITE_REQ:
            printf("GATTS_PREP_WRITE_REQ\n");
        break;

        case CYBLE_EVT_GATTS_EXEC_WRITE_REQ:
            printf("GATTS_EXEC_WRITE_REQ\n");
        break;

        case CYBLE_EVT_GATTS_HANDLE_VALUE_CNF:
            printf("GATTS_HANDLE_VALUE_CNF\n");
        break;

        case CYBLE_EVT_GATTS_DATA_SIGNED_CMD_REQ:
            printf("GATTS_DATA_SIGNED_CMD_REQ\n");
        break;


        /* GATT Server events (CYBLE_EVT_T) */

        case CYBLE_EVT_GATTS_INDICATION_ENABLED:
        {
            CYBLE_GATTS_WRITE_REQ_PARAM_T* write_req_param = (CYBLE_GATTS_WRITE_REQ_PARAM_T*)eventParam;
            printf("GATTS_INDICATION_ENABLED %d\n", write_req_param->handleValPair.attrHandle);
        }
        break;

        case CYBLE_EVT_GATTS_INDICATION_DISABLED:
        {
            CYBLE_GATTS_WRITE_REQ_PARAM_T* write_req_param = (CYBLE_GATTS_WRITE_REQ_PARAM_T*)eventParam;
            printf("GATTS_INDICATION_DISABLED %d\n", write_req_param->handleValPair.attrHandle);
        }
        break;


        /* L2CAP events */

        case CYBLE_EVT_L2CAP_CONN_PARAM_UPDATE_REQ:
        break;

        case CYBLE_EVT_L2CAP_CONN_PARAM_UPDATE_RSP:
        break;

        case CYBLE_EVT_L2CAP_COMMAND_REJ:
        break;

        case CYBLE_EVT_L2CAP_CBFC_CONN_IND:
        break;

        case CYBLE_EVT_L2CAP_CBFC_CONN_CNF:
        break;

        case CYBLE_EVT_L2CAP_CBFC_DISCONN_IND:
        break;

        case CYBLE_EVT_L2CAP_CBFC_DISCONN_CNF:
        break;

        case CYBLE_EVT_L2CAP_CBFC_DATA_READ:
        break;

        case CYBLE_EVT_L2CAP_CBFC_RX_CREDIT_IND:
        break;

        case CYBLE_EVT_L2CAP_CBFC_TX_CREDIT_IND:
        break;

        case CYBLE_EVT_L2CAP_CBFC_DATA_WRITE_IND:
        break;


        /* default catch-all case */

        default:
            printf("CYBLE_???\n");
        break;
    }
}
