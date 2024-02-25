/* C Standard library */
#include <stdio.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
//tähän talteen empty ja fullbuzzer funktiot
void emptyBuzzer() {
    buzzerOpen(hBuzzer);
    buzzerSetFrequency(8000);
    Task_sleep(200000 / Clock_tickPeriod); //pisin ja korkein ääni kriittisimmälle tilalle..
    buzzerClose;
}
void fullBuzzer() {
    buzzerOpen(hBuzzer);
    buzzerSetFrequency(4000);
    Task_sleep (125000 / Clock_tickPeriod); //pisin toiseksi pisin ja korkein ääni että sen erottaa muista...
    buzzerClose;
}
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

/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];


enum state { WAITING=1, DATA_READY };
enum state programState = WAITING;


double ambientLight = -1000.0;



static PIN_Handle buttonHandle;
static PIN_State buttonState;
static PIN_Handle ledHandle;
static PIN_State ledState;

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

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {

    // JTKJ: Tehtävä 4. Lisää UARTin alustus: 9600,8n1
    // JTKJ: Exercise 4. Setup here UART connection as 9600,8n1

    while (1) {
        if (programState == DATA_READY) {
            char data[6];
            sprintf(data,"%.f\n", ambientLight);
            programState == WAITING;
        }



        // JTKJ: Tehtävä 4. Lähetä sama merkkijono UARTilla
        // JTKJ: Exercise 4. Send the same sensor data string with UART
        // Just for sanity check for exercise, you can comment this out
        //System_printf("uartTask\n");
        //System_flush();

        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle      i2c;
    I2C_Params      i2cParams;


    I2C_Params_init(&i2cParams);
       i2cParams.bitRate = I2C_400kHz;
       i2c = I2C_open(Board_I2C_TMP, &i2cParams);
          if (i2c == NULL) {
             System_abort("Error Initializing I2C\n");
          }
    // JTKJ: Tehtävä 2. Alusta sensorin OPT3001 setup-funktiolla
    //       Laita enne funktiokutsua eteen 100ms viive (Task_sleep)
          Task_sleep(1000000 / Clock_tickPeriod);
          opt3001_setup(&i2c);
    // JTKJ: Exercise 2. Setup the OPT3001 sensor for use
    //       Before calling the setup function, insert 100ms delay with Task_sleep

    while (1) {

        // JTKJ: Tehtävä 2. Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona
        // JTKJ: Exercise 2. Read sensor data and print it to the Debug window as string
        double valoisuus= opt3001_get_data(&i2c);
        char result[6];
        sprintf(result, "%.f\n", valoisuus);
        printf(result);
        // JTKJ: Tehtävä 3. Tallenna mittausarvo globaaliin muuttujaan
        //       Muista tilamuutos
        // JTKJ: Exercise 3. Save the sensor value into the global variable
        //       Remember to modify state
        if (programState == WAITING){
            double ambientLight = valoisuus;
            programState = DATA_READY;
        }


        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Int main(void) {

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Initialize board
    Board_initGeneral();


    // JTKJ: Tehtävä 2. Ota i2c-väylä käyttöön ohjelmassa
    // JTKJ: Exercise 2. Initialize i2c bus
    Board_initI2C();
    // JTKJ: Tehtävä 4. Ota UART käyttöön ohjelmassa
    // JTKJ: Exercise 4. Initialize UART

    buttonHandle = PIN_open(&buttonState, buttonConfig);
   if(!buttonHandle) {
      System_abort("Error initializing button pins\n");
   }
   ledHandle = PIN_open(&ledState, ledConfig);
   if(!ledHandle) {
      System_abort("Error initializing LED pins\n");
   }
      if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
            System_abort("Error registering button callback function");
     }

    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);

}
