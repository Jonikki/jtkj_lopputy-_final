/* C Standard library */
#include <stdio.h>
#include <math.h>
#include <string.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/i2c/I2CCC26XX.h>

/* Board Header files */
#include "Board.h"
#include "sensors/mpu9250.h"
#include "buzzer.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];


enum state { WAITING=1, DATA_READY };
enum state programState = WAITING;


//M‰‰ritell‰‰n kiihtyvyyden ja gyroskoopin (globaalit) muuttujat, jos ei tarvita kommentoidaan pois
//float ax, ay, az, gx, gy, gz;

//M‰‰ritell‰‰n globaali muuttuja command, joka l‰hetet‰‰n uart:lla...
//Sovitaan samalla commandin arvot. 0 = Ei komentoja, 1 = EXERCISE(3), 2 = EAT(3), 3 = PET(3) ja 4 = ACTIVATE(3).
//K‰ytet‰‰n joka komennolla arvoa 3, niin ei tarvitse testauksessa odottaa ett‰ palkit tippuu esim. <5...
int command = 0;

//Tehd‰‰n heti per‰‰n vaikka muuttujan command - m‰‰rittelyfunktio, jota voidaan k‰ytt‰‰ suoraan sensorTaskFxn:ss‰, p‰‰nvaivan s‰‰st‰miseksi
//T‰t‰ varten on otettu k‰yttˆˆn math.h kirjasto, jotta saadaan funktiosta selkolukuisempi k‰ytt‰m‰ll‰ komentoa fabs(), joka voi lukea liukuluvun itseisarvon.
void determineCommand(float AX, float AY, float AZ, float GX, float GY, float GZ){

    if (fabs(AX) > 15) {
        command = 1;
    } else if (fabs(AY) > 15) {
        command = 2;
    } else if (AZ > 20){
        command = 3;
    } else if (AZ < -25){
        command = 4;
    } else if (fabs(AX) < 30 && fabs(AY) < 30 && fabs(AZ) < 30){
        command = 0;
    }
}
//Funktiossa on myˆs otettu mukaan gyroskoopin arvot GX, GY ja GZ, jotta niill‰ voidaan myˆhemmin m‰‰ritell‰ lis‰komentoja, jos n‰in halutaan.


//MPU9250-Sensorin virtapinnien globaalit muuttujat;
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

//Virtapinni;
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};
//MPU:lle oma I2C k‰yttˆliittym‰;
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};


static PIN_Handle ledHandle;
static PIN_Handle hBuzzer;
static PIN_State sBuzzer;

//Kaiuttimen alustus
PIN_Config cBuzzer[] = {
  Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
  PIN_TERMINATE
};
//Otetaan heti per‰‰n vaikka kaiuttimen k‰yttˆfunktioiden m‰‰rittely, jota voidaan k‰ytt‰‰ UART:n while-loopissa..
//Tunnistettu k‰sky
void playBuzzer() {
    buzzerOpen(hBuzzer);
    buzzerSetFrequency(2000);
    Task_sleep(50000 / Clock_tickPeriod); //Annetaan tarpeeksi pitk‰ ‰‰ni, ett‰ sen tajuaa..
    buzzerClose();
}
//Viesti vastaanotettu (id,BEEP)
void messageBuzzer() {
    buzzerOpen(hBuzzer);
    buzzerSetFrequency(6000);
    Task_sleep(200000 / Clock_tickPeriod);
    buzzerClose();
}
//Tehd‰‰n t‰h‰n myˆs vakiot ja funktiot buzzerFlag:lle...
//k‰ytet‰‰n t‰ss‰ volatilea, jotta ohjelma tiet‰‰ ett‰ muuttuja buzzerFlag voi muuttua uartCallbackissa, samalla kun sit‰ muutetaan buzzerTaskFxn
volatile bool buzzerFlag = false;
//Funktio joka asettaa buzzerFlag:n trueksi, jotta buzzerTaskFxn tiet‰‰ soittaa kaiutinta
void setbuzzerFlag() {
    buzzerFlag = true;
}
//Funktio jolla voi tarkistaa onko flag true/false.. Periaatteessa tarpeeton, jonkun muun n‰kˆkulmasta helpompi tunnistaa mit‰ buzzerTaskFxn:ss‰ tapahtuu.
bool buzzerFlagisSet() {
    return buzzerFlag;
}
//Seuraavaksi nollataan buzzerFlag.
void clearbuzzerFlag() {
    buzzerFlag = false;
}
//J‰tet‰‰n ledin ja napin alustus, tulevaisuuden varalta
PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

PIN_Config ledConfig[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
       uint_t pinValue = PIN_getOutputValue( Board_LED1 );
          pinValue = !pinValue;
          PIN_setOutputValue( ledHandle, Board_LED1, pinValue );
};

char receivedmessage[81];
void uartCallback(UART_Handle uart, void *rxBuf, size_t size) {

}

//Taskit:
void uartTaskFxn(UArg arg0, UArg arg1) {
    // UART:in alustus, sama kuin harjoituksissa?
    UART_Handle uart;
    UART_Params uartParams;
    //Luodaan viesteille valmiit taulukot, jossa l‰hetettyjen viestien max.pituus on 23 + 1, ja vastaanotetut viestit ovat aina 80 + 1
    char message[24];
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    //vaihdetaan readMode callbackiin, Oli aiemmin blocking... Lis‰t‰‰n t‰h‰n myˆs uartParams.readCallback = uartCallback;
    uartParams.readMode=UART_MODE_CALLBACK;
    uartParams.readCallback = uartCallback;
    uartParams.baudRate = 9600;
    uartParams.dataLength = UART_LEN_8;
    uartParams.parityType = UART_PAR_NONE;
    uartParams.stopBits = UART_STOP_ONE;
    uart = UART_open(Board_UART0, &uartParams);
       if (uart == NULL) {
          System_abort("Error opening the UART");
       }


//Seuraavaksi tilakoneella muodostetaan oikea viesti, command-arvosta riippuen. K‰ytet‰‰n edell‰ m‰‰riteltyj‰ arvoja: 1 = EXERCISE(3), 2 = EAT(3), 3 = PET(3) ja 4 = ACTIVATE(3).
    while (1) {
        if (programState == DATA_READY) {
            System_printf("Komento tunnistettu!\n");
            if (command == 1) {
                playBuzzer();
                //X-akselin kiihtyvyydell‰ nostetaan EXERCISE-Palkkia 3 pyk‰l‰‰
                sprintf(message, "id:3111,EXERCISE:3\0");
            } else if (command == 2) {
                playBuzzer();
                //Y-akselin kiihtyvyydell‰ nostetaan EAT-Palkkia 3 pyk‰l‰‰
                sprintf(message, "id:3111,EAT:3\0");
            } else if (command == 3) {
                playBuzzer();
                //Z-akselin positiivisen suunnan kiihtyvyydell‰ nostetaan PET-Palkkia 3 pyk‰l‰‰
                sprintf(message, "id:3111,PET:3\0");
            } else if (command == 4){
                playBuzzer();
                //Z-akselin negatiivisen suunnan kiihtyvyydell‰ nostetaan Kaikkia palkkeja 3 pyk‰l‰‰
                sprintf(message, "id:3111,ACTIVATE:3;3;3\0");
            }
            // T‰h‰n Datan kommunikaato taustaohjelman kanssa

            UART_write(uart, message , strlen(message) + 1);
            System_printf("Viesti l‰hetetty!\n");
            programState = WAITING;
        }
        //Vastaanotetaan viestej‰ UART:lta receivedmessage-puskurille
        UART_read(uart, (void*)receivedmessage, sizeof(receivedmessage));
        //Jos viestill‰ on sis‰ltˆ‰, tulostetaan viesti debug-konsoliin, ja tarkistetaan onko se meille
        if (receivedmessage[0] != '\0') {
            System_printf("%s\n", receivedmessage);
            if (strncmp(receivedmessage, "3111,BEEP", 9) == 0) {
                setbuzzerFlag();
            }
            //Tyhjennet‰‰n puskuri
            receivedmessage[0] = '\0';
        }
        //piip
        if (buzzerFlagisSet()) {
                    printf("Buzzer flag is set\n");
                    messageBuzzer();
                    clearbuzzerFlag();
                }


        Task_sleep(100000 / Clock_tickPeriod);
    }

}

void sensorTaskFxn(UArg arg0, UArg arg1) {

        I2C_Handle i2cMPU; // Sensorin oma I2C-k‰yttˆliittym‰
        I2C_Params i2cMPUParams;

        I2C_Params_init(&i2cMPUParams);
        i2cMPUParams.bitRate = I2C_400kHz;
        i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

        // Sensoriin virrat p‰‰lle
        PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

        // Odotetaan 100ms ett‰ sensori k‰ynnistyy
        Task_sleep(100000 / Clock_tickPeriod);

        i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
            if (i2cMPU == NULL) {
                System_abort("Error Initializing I2CMPU\n");
            }

    // Alustetaan MPU9250
          Task_sleep(1000000 / Clock_tickPeriod);
          mpu9250_setup(&i2cMPU);


    while (1) {

        // Testiksi koitetaan aluksi tulostaa data sensorilta debuggiin
        float AX, AY, AZ, GX, GY, GZ;
        mpu9250_get_data(&i2cMPU, &AX, &AY, &AZ, &GX, &GY, &GZ);
        AX *= 10;
        AY *= 10;
        AZ *= 10;
        char result[10];
        sprintf(result, "AX=%.2f, AY=%.2f, AZ=%.2f, GX=%.2f, GY=%.2f, GZ=%.2f\n", AX, AY, AZ, GX, GY, GZ);
        printf(result);

        // Suoritetaan t‰ss‰ v‰liss‰ komennon m‰‰rittely aiemmin tehdyll‰ determineCommand funktiolla.
        determineCommand (AX, AY, AZ, GX, GY, GZ);

        //T‰ss‰ tilakoneen tilan p‰ivitys, jos komento saatavilla, ja mit‰ jos komentoa ei ole havaittu.
        if (programState == WAITING && command != 0){
            programState = DATA_READY;
        }

        // 10ms v‰lein.
        Task_sleep(100000 / Clock_tickPeriod);
    }
}


int main(void) {

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;


    // Otetaan k‰yttˆˆn tarvittavat v‰yl‰t
    Board_initGeneral();
    Board_initI2C();
    Board_initUART();
    //Avataan kajarin ja sensorin pinnit:
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
      if (hBuzzer == NULL) {
        System_abort("Pin open failed!");
      }

    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
        if (hMpuPin == NULL) {
            System_abort("Pin open failed!");
        }

    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Sensor task create failed!");
    }



    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("UART task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();
    /* Start BIOS */
    BIOS_start();

    return (0);

}
//Suunnittelu; Joni, Jirko ja Valtteri, Toteutus; Joni ja Valtteri, testaus; Jirko ja Joni
