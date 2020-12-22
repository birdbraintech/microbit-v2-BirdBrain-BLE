#include "MicroBit.h"
#include "BirdBrain.h"
#include "BLESerial.h"

//uint8_t ble_read_buff[BLE__MAX_PACKET_LENGTH]; // incoming commands from the tablet/computer
//uint8_t sensor_vals[BLE__MAX_PACKET_LENGTH]; // sensor data to send to the tablet/computer
bool bleConnected = false; // Holds if connected over BLE
bool notifyOn = false; // Holds if notifications are being sent
MicroBitUARTService *bleuart;


uint8_t sense_count = 0;

void onConnected(MicroBitEvent)
{
    bleConnected = true;
    playConnectSound();
}

void onDisconnected(MicroBitEvent)
{
    bleConnected = false;
    notifyOn = false; // in case this was not reset by the computer/tablet
    playDisconnectSound();
    flashInitials();
}

void flashInitials()
{
    uint8_t count = 0;
    while(!bleConnected)
    {
        // Print one of three initials
        uBit.display.printAsync(initials_name[count]);
        fiber_sleep(400);
        uBit.display.clear();
        fiber_sleep(200);
        count++;
        // If you're at 3, spend a longer time with display cleared so it's obvious which initial is first
        if(count ==3)
        {
            fiber_sleep(500);
            count = 0;
        }
    }
}

// Initializes the UART
void bleSerialInit(ManagedString devName) 
{
    bleuart = new MicroBitUARTService(*uBit.ble, 32, 32);

    fiber_sleep(10); //Give the UART service a moment to register

    // Configure advertising with the UART service added, and with our prefix added
    uBit.ble->configAdvertising(devName);
    
    // Error checking code - uncomment if you need to check this
    //err= uBit.ble->configAdvertising(devName);
    //uint32_t *err;
    //char buffer[9];    
    /*
    for(int i = 0; i < 4; i++) {
        sprintf(buffer,"%lX",*(err+i)); //buffer now contains sn as a null terminated string
        ManagedString serialNumberAsHex(buffer);
        uBit.display.scroll(serialNumberAsHex);
    }*/
    uBit.ble->setTransmitPower(7); 
    uBit.ble->advertise();
    
    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_CONNECTED, onConnected);
    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_DISCONNECTED, onDisconnected);
    flashInitials(); // Start flashing since we're disconnected
}

// Checks what command (setAll, get firmware, etc) is coming over BLE, then acts as necessary
void bleSerialCommand()
{
    uint8_t buff_length = bleuart->rxBufferedSize(); // checking how many bytes we have in the buffer
    // Execute a command if we have data in our buffer and we're connected to a device
    if(bleConnected && (buff_length > 0))
    {
        uint8_t ble_read_buff[buff_length];
        bleuart->read(ble_read_buff, buff_length, ASYNC); 
        switch(ble_read_buff[0])
        {
            case SET_LEDARRAY:
                decodeAndSetDisplay(ble_read_buff);
                break;
            case SET_FIRMWARE:
                returnFirmwareData();
                break;
            case NOTIFICATIONS:
                if(ble_read_buff[1] == START_NOTIFY) {
                    notifyOn = true;
                }
                else if(ble_read_buff[1] == STOP_NOTIFY) {
                    notifyOn = false;
                }
                break;
        }
    }

}

// Collects the notification data and sends it to the computer/tablet
void assembleSensorData()
{
    if(bleConnected && notifyOn)
    {
        // return dummy sensor data for now
        if(whatAmI == A_FINCH)
        {
            uint8_t sensor_vals[FINCH_SENSOR_SEND_LENGTH];
            memset(sensor_vals, 0, FINCH_SENSOR_SEND_LENGTH);    
            bleuart->send(sensor_vals, sizeof(sensor_vals), ASYNC);
        }
        else
        {
            uint8_t sensor_vals[SENSOR_SEND_LENGTH];
            memset(sensor_vals, 0, SENSOR_SEND_LENGTH);
            // hard coding some sensor data
            sensor_vals[0] = 255-sense_count;
            sensor_vals[1] = 255;
            sensor_vals[2] = 42;
            sensor_vals[13] = sense_count;
            sense_count++;
            bleuart->send(sensor_vals, sizeof(sensor_vals), ASYNC);
        }
    }
}

void decodeAndSetDisplay(uint8_t displayCommands[])
{
    if (displayCommands[1] & SYMBOL) //In this case we are going to display a symbol 
    {
        MicroBitImage bleImage(5,5);
        
        uint32_t imageVals = 0; // puts all 25 bits into a single value
        uint8_t currentByte = 0;
        for(int i = 2; i<6;i++)
        {
            currentByte = displayCommands[i];
            imageVals   = imageVals | currentByte<<(24-8*(i-2));
        }
        for(int row = 0; row < 5; row++)
        {
            for(int col = 0; col < 5; col++)
            {
                if(imageVals & 0x01<<(col*5+row)) 
                {
                    bleImage.setPixelValue(row, col, 255);
                }
                else
                {
                    bleImage.setPixelValue(row, col, 0);
                }
            }
        }
        uBit.display.clear();
        uBit.display.printAsync(bleImage);
    }
    else if(displayCommands[1] & SCROLL) // In this we will scroll a message
    {
        uBit.display.clear();
        uint8_t scroll_length = displayCommands[1] - SCROLL; // This gets us the length of the message to scroll
        if(scroll_length <= 18) // Need to protect against invalid commands
        {
            char scrollVals[scroll_length];
            for(int i = 0; i < scroll_length; i++)
            {
                scrollVals[i] = displayCommands[i+2];
            }
            ManagedString scrollMsg(scrollVals);
            uBit.display.scrollAsync(scrollMsg);
        }
    }
    else // if neither of these, clear the display
    {
        uBit.display.clear();
    }
}

void returnFirmwareData()
{
    // hardware version is 1 for NXP, 2 for LS - currently uses LS
    // second byte is micro_firmware_version - 0x02 on V1
    // third byte is SAMD firmware version - 0 for MB, 3 for HB, 7 or 44 for Finch
    // Hard coding this for now
    uint8_t return_buff[3];
    return_buff[0] = 2;
    return_buff[1] = 2;
    if(whatAmI == A_MB)
    {
        return_buff[2] = 0xFF;
    } 
    else if(whatAmI == A_HB)
    {
        return_buff[2] = 3;
    } 
    else if(whatAmI == A_FINCH)
    {
        return_buff[2] = 44;
    } 
    else
    {
        return_buff[2] = 0;
    }
    bleuart->send(return_buff, 3, ASYNC);
}


// Plays the connect sound
void playConnectSound()
{
    // Plays the BirdBrain connect song, over the built-in speaker if a micro:bit, or over Finch/HB buzzer if not 
    if(whatAmI == A_MB)
    {
        uBit.io.speaker.setAnalogValue(512);
        uBit.io.speaker.setAnalogPeriodUs(3039);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(1912);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(1703);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(1351);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogValue(0);
    }
    else 
    {
        uBit.io.P0.setAnalogValue(512);
        uBit.io.P0.setAnalogPeriodUs(3039);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(1912);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(1703);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(1351);
        uBit.sleep(100);
        uBit.io.P0.setAnalogValue(0);
    }    
}

// Plays the disconnect sound
void playDisconnectSound()
{
    // Plays the BirdBrain disconnect song
    if(whatAmI == A_MB)
    {
        uBit.io.speaker.setAnalogValue(512);
        uBit.io.speaker.setAnalogPeriodUs(1702);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(2024);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(2551);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogPeriodUs(3816);
        uBit.sleep(100);
        uBit.io.speaker.setAnalogValue(0);
    }
    else
    {
        uBit.io.P0.setAnalogValue(512);
        uBit.io.P0.setAnalogPeriodUs(1702);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(2024);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(2551);
        uBit.sleep(100);
        uBit.io.P0.setAnalogPeriodUs(3816);
        uBit.sleep(100);
        uBit.io.P0.setAnalogValue(0);
    }    
}
