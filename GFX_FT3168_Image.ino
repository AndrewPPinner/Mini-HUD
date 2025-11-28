#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "Material_16Bit_466x466px.h"
#include <FreeSansBold24pt7b.h>
#include "canvas/Arduino_Canvas.h"
#include <TinyGPS++.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "Preferences.h"

static uint8_t Image_Flag = 0;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
    LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RST /* RST */,
                                      0 /* rotation */, false /* IPS */, LCD_WIDTH, LCD_HEIGHT,
                                      6 /* col offset 1 */, 0 /* row offset 1 */, 0 /* col_offset2 */, 0 /* row_offset2 */);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS,
                                                       DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void)
{
    FT3168->IIC_Interrupt_Flag = true;
}

#define RXD2 42
#define TXD2 47

#define GPS_BAUD 9600

// Create an instance of the HardwareSerial class for Serial 2
HardwareSerial gpsSerial(2);
Preferences prefs;

int16_t mphX, mphY;  // Position of "mph"
uint16_t mphW, mphH; // Size of "mph"

int16_t digitX[3];   // X positions for each digit
int16_t digitY;      // Common Y position
uint16_t digitW, digitH;
uint32_t displayed_password;
uint16_t bgColor = WHITE;
uint16_t fgColor = BLACK;
bool showDirections = false;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("Client address: %s\n", connInfo.getAddress().toString().c_str());

        /**
         *  We can use the connection handle here to ask for different connection parameters.
         *  Args: connection handle, min connection interval, max connection interval
         *  latency, supervision timeout.
         *  Units; Min/Max Intervals: 1.25 millisecond increments.
         *  Latency: number of intervals allowed to skip.
         *  Timeout: 10 millisecond increments.
         */
        pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("Client disconnected - start advertising\n");
        NimBLEDevice::startAdvertising();
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        Serial.printf("MTU updated: %u for connection ID: %u\n", MTU, connInfo.getConnHandle());
    }

    /********************* Security handled here *********************/
    uint32_t onPassKeyDisplay() override {
        displayed_password = random(100000, 1000000); // 6-digit random
        
        gfx->setCursor((LCD_WIDTH / 2) - 20, LCD_HEIGHT - 50);
        gfx->setTextSize(1);
        gfx->print(displayed_password);
        
        Serial.printf("Passkey: %06u\n", displayed_password);
        return displayed_password;
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pass_key) override {
        Serial.printf("The passkey YES/NO number: %" PRIu32 "\n", pass_key);
        NimBLEDevice::injectConfirmPasskey(connInfo, pass_key == displayed_password);
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        gfx->fillRect((LCD_WIDTH / 2) - 35, LCD_HEIGHT - 100, 400, 100, bgColor);
        gfx->setTextSize(3);
        /** Check that encryption was successful, if not we disconnect the client */
        if (!connInfo.isEncrypted()) {
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
            Serial.printf("Encrypt connection failed - disconnecting client\n");
            return;
        }

        Serial.printf("Secured connection to: %s\n", connInfo.getAddress().toString().c_str());
    }
} serverCallbacks;

/** Handler class for characteristic actions */
class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    }

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        String jsonStr = pCharacteristic->getValue();
        const char* data = jsonStr.c_str();
        Serial.println(data);

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, data);
        if (err) {
            Serial.print("JSON error: ");
            Serial.println(err.f_str());
            return;
        }

        const char* maneuver = doc["maneuver"];
        int distance = doc["distance"];
        const char* instruct = doc["instructions"];
        const char* road = doc["roadName"];
        bool hideDirections = doc["hideDirections"];

        showDirections = !hideDirections;
        if (showDirections) {
            //TODO: Need to move ALL gfx writes to main thread with flags or some kind of mutex
            // also, don't rewrite and fill screen each time, only update the distance on distance changes
            // only do full text rewrite when updated direction, not distance
            gfx->fillScreen(bgColor);
            gfx->setTextSize(1);
            gfx->setCursor(60, 200);
            gfx->printf("%s : %d : %s", maneuver, distance, road);
        }
    }

    /**
     *  The value returned in code is the NimBLE host return code.
     */
    void onStatus(NimBLECharacteristic* pCharacteristic, int code) override {
        Serial.printf("Notification/Indication return code: %d, %s\n", code, NimBLEUtils::returnCodeToString(code));
    }

    /** Peer subscribed to notifications/indications */
    void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        
    }
} chrCallbacks;

/** Handler class for descriptor actions */
class DescriptorCallbacks : public NimBLEDescriptorCallbacks {
    void onWrite(NimBLEDescriptor* pDescriptor, NimBLEConnInfo& connInfo) override {
        std::string dscVal = pDescriptor->getValue();
        Serial.printf("Descriptor written value: %s\n", dscVal.c_str());
    }

    void onRead(NimBLEDescriptor* pDescriptor, NimBLEConnInfo& connInfo) override {
        Serial.printf("%s Descriptor read\n", pDescriptor->getUUID().toString().c_str());
    }
} dscCallbacks;

#define BLEDeviceName "Mini-DASH"
bool initBluetooth() {
    NimBLEDevice::init(BLEDeviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    
    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService* pDeadService = pServer->createService("DEAD");
    NimBLECharacteristic* pBeefCharacteristic = pDeadService->createCharacteristic("BEEF", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);

    pBeefCharacteristic->setValue("Burger");
    pBeefCharacteristic->setCallbacks(&chrCallbacks);
    pDeadService->start();
    
    /**
     *  2902 and 2904 descriptors are a special case, when createDescriptor is called with
     *  either of those uuid's it will create the associated class with the correct properties
     *  and sizes. However we must cast the returned reference to the correct type as the method
     *  only returns a pointer to the base NimBLEDescriptor class.
     */
    NimBLE2904* pBeef2904 = pBeefCharacteristic->create2904();
    pBeef2904->setFormat(NimBLE2904::FORMAT_UTF8);
    pBeef2904->setCallbacks(&dscCallbacks);
    
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->enableScanResponse(true);
    adv->addServiceUUID(pDeadService->getUUID());
    adv->setName(BLEDeviceName);

    return adv->start();
}

bool initGPS() {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
    Serial.println("GPS Serial started at 9600 baud rate");
    return true;
}

bool initScreen() {
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);
    
    while (FT3168->begin() == false)
    {
        Serial.println("FT3168 initialization fail");
        delay(2000);
    }
    Serial.println("FT3168 initialization successfully");
    
    gfx->begin(80000000);
    gfx->fillScreen(WHITE);
    
    uint8_t brightnessPref = prefs.getUChar("brightness", 255);
    Serial.println(brightnessPref);
    for (int i = 0; i <= brightnessPref; i++)
    {
        gfx->Display_Brightness(i);
        delay(3);
    }
    Serial.printf("ID: %#X \n\n", (int32_t)FT3168->IIC_Device_ID());
    delay(1000);
    
    gfx->setFont(&FreeSansBold24pt7b);
    gfx->setTextSize(3);
    
    // Get bounding box for " mph"
    gfx->getTextBounds(" mph", 0, 0, &mphX, &mphY, &mphW, &mphH);
    
    // Position "mph" near vertical center bottom
    mphX = (LCD_WIDTH - mphW) / 2;
    mphY = (LCD_HEIGHT + mphH) / 2 + 10;
    
    // Draw "mph" label once
    gfx->setTextColor(BLACK);
    gfx->setCursor(mphX, mphY);
    gfx->print(" mph");
    
    // Measure size of a single digit
    int16_t dummyX, dummyY;
    gfx->getTextBounds("0", 0, 0, &dummyX, &dummyY, &digitW, &digitH);
    
    // Digit Y position
    digitY = mphY - digitH - 10;
    
    // Calculate X positions of each digit (centered)
    int totalWidth = 3 * digitW;
    int startX = (LCD_WIDTH - totalWidth) / 2;
    for (int i = 0; i < 3; i++)
    {
        digitX[i] = startX + i * digitW;
    }

    return true;
}

void setup() {
    Serial.begin(115200);
    prefs.begin("mini-dash", false);

    initBluetooth();
    initGPS();
    initScreen();
}

int speed = 0;
bool nightMode = false;
char prevDigits[4] = "000";

void drawSpeedDigits(int speed) {    
    char newDigits[4];
    snprintf(newDigits, sizeof(newDigits), "%03d", speed);  // Always 3 digits

    for (int i = 0; i < 3; i++)
    {
        if (newDigits[i] != prevDigits[i])
        {
            // Erase digit area
            gfx->fillRect(digitX[i], digitY - digitH, digitW, digitH + 5, bgColor);

            // Draw digit
            gfx->setTextSize(3);
            gfx->setCursor(digitX[i], digitY);
            gfx->setTextColor(fgColor);
            gfx->print(newDigits[i]);

            prevDigits[i] = newDigits[i];
        }
    }
}

int brightness = 255;
TinyGPSPlus gps;
const int gestureThreshold = 4;


//TODO: Must clean up this code, make a screen matrix and wrap write functions to make sure data doesn't overlap or look bad
// Update all write functions to handle the resizing, fonts, screen wiping, and most importantly rewrite tiling so we don't have to write the full screen everytime
void loop() {
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.speed.isUpdated()) {
        speed = (int)gps.speed.mph();
    }

    int i = 0;
    int startingY = 0;
    bool clearBrightnessText = false;
    // Read touch input
    while(FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER) == 1) {
        if (i == 1)
        {
            startingY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        }
        if (i > gestureThreshold) {
            int currentY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
            int deltaY = startingY - currentY;

            brightness += deltaY;

            if (brightness > 255) brightness = 255;
            if (brightness < 20)  brightness = 20;

            gfx->Display_Brightness(brightness);
            gfx->setCursor(LCD_WIDTH / 2 + 30, LCD_HEIGHT - 50);
            gfx->setTextSize(1);
            gfx->print(brightness);

            startingY = currentY;
            clearBrightnessText = true;
            gfx->fillRect(LCD_WIDTH / 2, LCD_HEIGHT - 100, 150, 100, bgColor);
        }
        i++;
        delay(50);
    }

    if (i < gestureThreshold && i > 0)
    {
        nightMode = !nightMode;
        bgColor = nightMode ? BLACK : WHITE;
        fgColor = nightMode ? WHITE : BLACK;
        gfx->fillScreen(bgColor);
        gfx->setTextColor(fgColor);
        gfx->setCursor(mphX, mphY);
        gfx->setTextSize(3);
        gfx->print(" mph");
    }

    if (clearBrightnessText)
    {
        prefs.putUChar("brightness", brightness);
        gfx->fillRect(LCD_WIDTH / 2, LCD_HEIGHT - 100, 150, 100, bgColor);
        gfx->setTextSize(3);
    }
    
    if (!showDirections) {
        drawSpeedDigits(speed);
    }
}
