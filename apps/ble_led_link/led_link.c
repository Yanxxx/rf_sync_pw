/*
 * ble_led_link: BLE link test between two LP_CC2652R7 boards.
 *
 * Built twice (see BUILD.bazel):
 *   ble_led_peripheral  advertises as "PW-LED-P" and exposes TI's Simple GATT
 *                       profile; every write to characteristic 1 (UUID 0xFFF1)
 *                       sets the GREEN LED to bit 0 of the written value.
 *   ble_led_central     (LED_LINK_CENTRAL) scans for "PW-LED-P", connects,
 *                       discovers characteristic 0xFFF1 and writes 0,1,0,1...
 *                       every LED_LINK_PERIOD_MS (write *with* response). The GREEN LED
 *                       shows the last value the peripheral acknowledged.
 *
 * LEDs on both boards:
 *   RED   (D106, CONFIG_GPIO_LED_RED)   on while the BLE link is up
 *   GREEN (D107, CONFIG_GPIO_LED_GREEN) toggles once per delivered message
 * So with a working link both green LEDs toggle in step.
 *
 * Messages are also logged with pw_log (sequence numbers) on the XDS110 UART.
 * Uses TI's BLEAppUtil framework (the same APIs as TI's basic_ble).
 */

#define PW_LOG_MODULE_NAME "LINK"

#include <string.h>

#include <ti/display/Display.h>
#include <ti/drivers/GPIO.h>

#include "ti_ble_config.h"
#include "ti_drivers_config.h"
#include <ti/bleapp/ble_app_util/inc/bleapputil_api.h>
#include <ti/bleapp/profiles/simple_gatt/simple_gatt_profile.h>

#include "pw_log/log.h"

#include "led_link.h"

/* Name the peripheral advertises and the central looks for. */
static const char kPeerName[] = "PW-LED-P";

/* pw_log output on this board goes through TI's UART Display (see
 * targets/lp_cc2652r7/sys_io_display.cc), which looks for this handle. */
Display_Handle dispHandle = NULL;

static void setLed(uint_least8_t led, uint8_t on)
{
    GPIO_write(led, on ? CONFIG_GPIO_LED_ON : CONFIG_GPIO_LED_OFF);
}

/* ------------------------------------------------------------------------ */
/* BLEAppUtil setup (same parameters as TI's basic_ble app_main.c)          */
/* ------------------------------------------------------------------------ */

static BLEAppUtil_GeneralParams_t appMainParams =
{
    .taskPriority = 1,
    .taskStackSize = 1024,
    .profileRole = (BLEAppUtil_Profile_Roles_e)(HOST_CONFIG),
    .addressMode = DEFAULT_ADDRESS_MODE,
    .deviceNameAtt = attDeviceName,
    .pDeviceRandomAddress = pRandomAddress,
};

static BLEAppUtil_PeriCentParams_t appMainPeriCentParams =
{
    .connParamUpdateDecision = DEFAULT_PARAM_UPDATE_REQ_DECISION,
    .gapBondParams = &gapBondParams
};

static void criticalErrorHandler(int32 errorCode, void *pInfo)
{
    (void)pInfo;
    PW_LOG_ERROR("BLEAppUtil critical error %d", (int)errorCode);
}

/* ------------------------------------------------------------------------ */
/* Connection events (both roles)                                           */
/* ------------------------------------------------------------------------ */

static uint16_t linkConnHandle = LINKDB_CONNHANDLE_INVALID;
static void onLinkUp(uint16_t connHandle);
static void onLinkDown(void);

static void connEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
    switch (event)
    {
    case BLEAPPUTIL_LINK_ESTABLISHED_EVENT:
    {
        gapEstLinkReqEvent_t *est = (gapEstLinkReqEvent_t *)pMsgData;
        if (est->hdr.status != SUCCESS)
        {
            PW_LOG_WARN("Connection failed, status 0x%02x", est->hdr.status);
            onLinkDown();
            break;
        }
        linkConnHandle = est->connectionHandle;
        setLed(CONFIG_GPIO_LED_RED, 1);
        PW_LOG_INFO("Connected to %s (handle 0x%04x)",
                    BLEAppUtil_convertBdAddr2Str(est->devAddr), linkConnHandle);
        onLinkUp(linkConnHandle);
        break;
    }
    case BLEAPPUTIL_LINK_TERMINATED_EVENT:
    {
        gapTerminateLinkEvent_t *term = (gapTerminateLinkEvent_t *)pMsgData;
        PW_LOG_WARN("Disconnected, reason 0x%02x", term->reason);
        linkConnHandle = LINKDB_CONNHANDLE_INVALID;
        setLed(CONFIG_GPIO_LED_RED, 0);
        onLinkDown();
        break;
    }
    case BLEAPPUTIL_LINK_PARAM_UPDATE_REQ_EVENT:
    {
        /* DEFAULT_PARAM_UPDATE_REQ_DECISION passes these to the app: accept. */
        BLEAppUtil_paramUpdateRsp((gapUpdateLinkParamReqEvent_t *)pMsgData, TRUE);
        break;
    }
    default:
        break;
    }
}

static BLEAppUtil_EventHandler_t connHandler =
{
    .handlerType   = BLEAPPUTIL_GAP_CONN_TYPE,
    .pEventHandler = connEventHandler,
    .eventMask     = BLEAPPUTIL_LINK_ESTABLISHED_EVENT |
                     BLEAPPUTIL_LINK_TERMINATED_EVENT |
                     BLEAPPUTIL_LINK_PARAM_UPDATE_REQ_EVENT,
};

#ifndef LED_LINK_CENTRAL
/* ======================================================================== */
/* Peripheral                                                               */
/* ======================================================================== */

/* Flags + complete local name "PW-LED-P" + Simple GATT service UUID. */
static uint8_t advData[] =
{
    0x02, GAP_ADTYPE_FLAGS, GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED | GAP_ADTYPE_FLAGS_GENERAL,
    0x09, GAP_ADTYPE_LOCAL_NAME_COMPLETE, 'P', 'W', '-', 'L', 'E', 'D', '-', 'P',
    0x03, GAP_ADTYPE_16BIT_MORE, LO_UINT16(SIMPLEGATTPROFILE_SERV_UUID), HI_UINT16(SIMPLEGATTPROFILE_SERV_UUID),
};

static uint8_t advHandle;
static uint32_t rxCount = 0;

static const BLEAppUtil_AdvInit_t advInit =
{
    .advDataLen      = sizeof(advData),
    .advData         = advData,
    .scanRespDataLen = sizeof(scanResData1),
    .scanRespData    = scanResData1,
    .advParam        = &advParams1,
};

static const BLEAppUtil_AdvStart_t advStart =
{
    .enableOptions       = GAP_ADV_ENABLE_OPTIONS_USE_MAX,
    .durationOrMaxEvents = 0,
};

static void startAdvertising(void)
{
    bStatus_t status = BLEAppUtil_advStart(advHandle, &advStart);
    PW_LOG_INFO("Advertising as \"%s\" (status 0x%02x)", kPeerName, status);
}

/* Called by the Simple GATT profile when a client writes a characteristic. */
static void simpleGattChangeCB(uint8_t paramId)
{
    if (paramId != SIMPLEGATTPROFILE_CHAR1)
    {
        return;
    }
    uint8_t value = 0;
    SimpleGattProfile_getParameter(SIMPLEGATTPROFILE_CHAR1, &value);
    rxCount++;
    setLed(CONFIG_GPIO_LED_GREEN, value & 1);
    PW_LOG_INFO("RX #%u: value=%u -> GREEN %s", (unsigned)rxCount, value,
                (value & 1) ? "on" : "off");
}

static SimpleGattProfile_CBs_t simpleGattCBs =
{
    .pfnSimpleGattProfile_Change = simpleGattChangeCB,
};

static void onLinkUp(uint16_t connHandle)
{
    (void)connHandle;
}

static void onLinkDown(void)
{
    setLed(CONFIG_GPIO_LED_GREEN, 0);
    startAdvertising();
}

static void roleStart(void)
{
    SimpleGattProfile_addService();
    SimpleGattProfile_registerAppCBs(&simpleGattCBs);
    BLEAppUtil_initAdvSet(&advHandle, &advInit);
    startAdvertising();
}

void LedLink_onTick(char *unused)
{
    (void)unused;
}

#else  /* LED_LINK_CENTRAL */
/* ======================================================================== */
/* Central                                                                  */
/* ======================================================================== */

static const BLEAppUtil_ScanInit_t scanInit =
{
    .primPhy         = DEFAULT_SCAN_PHY,
    .scanType        = DEFAULT_SCAN_TYPE,
    .scanInterval    = DEFAULT_SCAN_INTERVAL,
    .scanWindow      = DEFAULT_SCAN_WINDOW,
    .advReportFields = ADV_RPT_FIELDS,
    .scanPhys        = DEFAULT_SCAN_PHY,
    .fltPolicy       = SCANNER_FILTER_POLICY,
    .fltPduType      = SCANNER_FILTER_PDU_TYPE,
    .fltMinRssi      = SCANNER_FILTER_MIN_RSSI,
    .fltDiscMode     = SCANNER_FILTER_DISC_MODE,
    .fltDup          = SCANNER_DUPLICATE_FILTER,
};

static const BLEAppUtil_ConnParams_t connInit =
{
    .initPhys        = DEFAULT_INIT_PHY,
    .scanInterval    = INIT_PHYPARAM_SCAN_INT,
    .scanWindow      = INIT_PHYPARAM_SCAN_WIN,
    .minConnInterval = INIT_PHYPARAM_MIN_CONN_INT,
    .maxConnInterval = INIT_PHYPARAM_MAX_CONN_INT,
    .connLatency     = INIT_PHYPARAM_CONN_LAT,
    .supTimeout      = INIT_PHYPARAM_SUP_TO,
};

static const BLEAppUtil_ScanStart_t scanStart =
{
    .scanPeriod   = 0,  /* continuous */
    .scanDuration = 0,
    .maxNumReport = 0,
};

typedef enum { ST_SCANNING, ST_CONNECTING, ST_DISCOVERING, ST_READY } CentralState;

static CentralState state = ST_SCANNING;
static BLEAppUtil_ConnectParams_t peer = { .phys = INIT_PHY_1M, .timeout = 3000 };
static uint16_t char1Handle = 0;
static uint8_t nextValue = 1;
static uint8_t writeInFlight = 0;
static uint32_t txCount = 0;
static uint32_t ackCount = 0;

static void startScanning(void)
{
    state = ST_SCANNING;
    bStatus_t status = BLEAppUtil_scanStart(&scanStart);
    PW_LOG_INFO("Scanning for \"%s\" (status 0x%02x)", kPeerName, status);
}

/* True if the advertising data contains our peer's complete local name. */
static uint8_t isPeer(const uint8_t *data, uint16_t len)
{
    uint16_t i = 0;
    while (i + 1 < len)
    {
        uint8_t fieldLen = data[i];
        if (fieldLen == 0 || i + 1 + fieldLen > len)
        {
            return 0;
        }
        if (data[i + 1] == GAP_ADTYPE_LOCAL_NAME_COMPLETE &&
            fieldLen - 1 == sizeof(kPeerName) - 1 &&
            memcmp(&data[i + 2], kPeerName, sizeof(kPeerName) - 1) == 0)
        {
            return 1;
        }
        i += 1 + fieldLen;
    }
    return 0;
}

static void scanEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
    BLEAppUtil_ScanEventData_t *scan = (BLEAppUtil_ScanEventData_t *)pMsgData;
    switch (event)
    {
    case BLEAPPUTIL_ADV_REPORT:
    {
        bleStk_GapScan_Evt_AdvRpt_t *rpt = &scan->pBuf->pAdvReport;
        if (state == ST_SCANNING && isPeer(rpt->pData, rpt->dataLen))
        {
            peer.peerAddrType = rpt->addrType;
            memcpy(peer.pPeerAddress, rpt->addr, B_ADDR_LEN);
            PW_LOG_INFO("Found \"%s\" at %s, RSSI %d dBm", kPeerName,
                        BLEAppUtil_convertBdAddr2Str(rpt->addr), rpt->rssi);
            state = ST_CONNECTING;
            BLEAppUtil_scanStop();  /* connect once scanning is disabled */
        }
        break;
    }
    case BLEAPPUTIL_SCAN_DISABLED:
        if (state == ST_CONNECTING)
        {
            bStatus_t status = BLEAppUtil_connect(&peer);
            PW_LOG_INFO("Connecting (status 0x%02x)", status);
            if (status != SUCCESS)
            {
                startScanning();
            }
        }
        break;
    default:
        break;
    }
}

static BLEAppUtil_EventHandler_t scanHandler =
{
    .handlerType   = BLEAPPUTIL_GAP_SCAN_TYPE,
    .pEventHandler = scanEventHandler,
    .eventMask     = BLEAPPUTIL_ADV_REPORT | BLEAPPUTIL_SCAN_DISABLED,
};

static void discoverChar1(uint16_t connHandle)
{
    attReadByTypeReq_t req;
    req.startHandle = 0x0001;
    req.endHandle   = 0xFFFF;
    req.type.len    = ATT_BT_UUID_SIZE;
    req.type.uuid[0] = LO_UINT16(SIMPLEGATTPROFILE_CHAR1_UUID);
    req.type.uuid[1] = HI_UINT16(SIMPLEGATTPROFILE_CHAR1_UUID);
    state = ST_DISCOVERING;
    char1Handle = 0;
    bStatus_t status = GATT_DiscCharsByUUID(connHandle, &req, BLEAppUtil_getSelfEntity());
    PW_LOG_INFO("Discovering characteristic 0x%04x (status 0x%02x)",
                SIMPLEGATTPROFILE_CHAR1_UUID, status);
}

static void onLinkUp(uint16_t connHandle)
{
    discoverChar1(connHandle);
}

static void onLinkDown(void)
{
    char1Handle = 0;
    writeInFlight = 0;
    setLed(CONFIG_GPIO_LED_GREEN, 0);
    startScanning();
}

static void gattEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
    gattMsgEvent_t *msg = (gattMsgEvent_t *)pMsgData;
    (void)event;
    switch (msg->method)
    {
    case ATT_READ_BY_TYPE_RSP:
        if (state == ST_DISCOVERING && msg->msg.readByTypeRsp.numPairs > 0)
        {
            /* Each pair: decl handle(2) properties(1) value handle(2) uuid(2) */
            const uint8_t *p = msg->msg.readByTypeRsp.pDataList;
            char1Handle = BUILD_UINT16(p[3], p[4]);
        }
        if (msg->hdr.status == bleProcedureComplete && state == ST_DISCOVERING)
        {
            if (char1Handle != 0)
            {
                state = ST_READY;
                PW_LOG_INFO("Characteristic 0x%04x has value handle 0x%04x; sending every %d ms",
                            SIMPLEGATTPROFILE_CHAR1_UUID, char1Handle, LED_LINK_PERIOD_MS);
            }
            else
            {
                PW_LOG_ERROR("Characteristic 0x%04x not found", SIMPLEGATTPROFILE_CHAR1_UUID);
            }
        }
        break;
    case ATT_WRITE_RSP:
        writeInFlight = 0;
        ackCount++;
        /* The peripheral acknowledged value (nextValue ^ 1). */
        setLed(CONFIG_GPIO_LED_GREEN, (nextValue ^ 1) & 1);
        PW_LOG_INFO("ACK #%u: value=%u delivered", (unsigned)ackCount, (nextValue ^ 1) & 1);
        break;
    case ATT_ERROR_RSP:
        writeInFlight = 0;
        PW_LOG_WARN("ATT error: req 0x%02x handle 0x%04x code 0x%02x",
                    msg->msg.errorRsp.reqOpcode, msg->msg.errorRsp.handle,
                    msg->msg.errorRsp.errCode);
        break;
    default:
        break;
    }
}

static BLEAppUtil_EventHandler_t gattHandler =
{
    .handlerType   = BLEAPPUTIL_GATT_TYPE,
    .pEventHandler = gattEventHandler,
    .eventMask     = BLEAPPUTIL_ATT_READ_BY_TYPE_RSP |
                     BLEAPPUTIL_ATT_WRITE_RSP |
                     BLEAPPUTIL_ATT_ERROR_RSP,
};

/* Runs in the BLEAppUtil task, invoked every LED_LINK_PERIOD_MS by led_link_timer.cc. */
void LedLink_onTick(char *unused)
{
    (void)unused;
    if (state != ST_READY || writeInFlight || linkConnHandle == LINKDB_CONNHANDLE_INVALID)
    {
        return;
    }
    attWriteReq_t req;
    req.pValue = GATT_bm_alloc(linkConnHandle, ATT_WRITE_REQ, 1, NULL);
    if (req.pValue == NULL)
    {
        PW_LOG_WARN("GATT_bm_alloc failed");
        return;
    }
    req.handle = char1Handle;
    req.len = 1;
    req.pValue[0] = nextValue;
    req.sig = FALSE;
    req.cmd = FALSE;
    bStatus_t status = GATT_WriteCharValue(linkConnHandle, &req, BLEAppUtil_getSelfEntity());
    if (status != SUCCESS)
    {
        GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
        PW_LOG_WARN("Write failed to start (status 0x%02x)", status);
        return;
    }
    txCount++;
    writeInFlight = 1;
    PW_LOG_INFO("TX #%u: value=%u", (unsigned)txCount, nextValue);
    nextValue ^= 1;
}

static void roleStart(void)
{
    BLEAppUtil_registerEventHandler(&scanHandler);
    BLEAppUtil_registerEventHandler(&gattHandler);
    BLEAppUtil_scanInit(&scanInit);
    BLEAppUtil_setConnParams(&connInit);
    startScanning();
}
#endif /* LED_LINK_CENTRAL */

/* ------------------------------------------------------------------------ */
/* Startup                                                                  */
/* ------------------------------------------------------------------------ */

static void stackInitDoneHandler(gapDeviceInitDoneEvent_t *initDone)
{
    PW_LOG_INFO("%s: BLE stack up, ID address %s", APP_NAME,
                BLEAppUtil_convertBdAddr2Str(initDone->devAddr));
    BLEAppUtil_registerEventHandler(&connHandler);
    roleStart();
}

/* Called from main() (apps/ble_common/startup/main.c) before the scheduler. */
void appMain(void)
{
    GPIO_init();
    setLed(CONFIG_GPIO_LED_RED, 0);
    setLed(CONFIG_GPIO_LED_GREEN, 0);

    Display_Params params;
    Display_Params_init(&params);
    dispHandle = Display_open(Display_Type_UART, &params);
    if (dispHandle != NULL)
    {
        Display_clear(dispHandle);
    }

    LedLink_startTimer();
    BLEAppUtil_init(&criticalErrorHandler, &stackInitDoneHandler,
                    &appMainParams, &appMainPeriCentParams);
}
