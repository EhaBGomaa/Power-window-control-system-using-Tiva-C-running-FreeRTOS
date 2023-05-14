#include <stdint.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include "task.h"
#include <time.h>
#include <stdbool.h>
#include <semphr.h>
#include "driverlib/gpio.c"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "inc/hw_ints.h"
#include "inc/hw_gpio.h"
#include "tm4c123gh6pm.h"
#include "buttons.h"

#define Set_Bit(reg, bit) {reg |= (1U << bit);}
#define Clear_Bit(reg, bit) {reg &= ~(1U << bit);}
#define Toggle_Bit(reg, bit) {reg ^= (1U << bit);}
#define Get_Bit(reg, bit) ((reg & (1U << bit)) >> bit);
#define PortF_IRQn ((IRQn_Type) 30 )
#define PortB_IRQn ((IRQn_Type) 1 )

#define INT_GPIOA               16          // GPIO Port A
#define INT_GPIOB               17          // GPIO Port B
#define INT_GPIOC               18          // GPIO Port C
#define INT_GPIOD               19          // GPIO Port D
#define INT_GPIOE               20          // GPIO Port E


struct Window {
	bool isFullyClosed;
	bool isFullyOpened;
	bool isLocked;
	bool autoMode;
};

static struct Window CarWindow; // define an instance of the Window struct
static struct Button PortC_Buttons[4]; // define an array of buttons on Port C
static SemaphoreHandle_t jamSemaphore; // declare a semaphore for handling window jams
static SemaphoreHandle_t autoModeSemaphore; // declare a semaphore for handling auto mode


void CheckButtons(void *p); // function to check the status of buttons
void init(void); // function to initialize the hardware
void initStructs(void); // function to initialize the struct instances
void jamHandler(void *p); // function to handle window jams
void autoModeHandler(void *p); // function to handle auto mode
void jamInterrupt(void); // interrupt handler for window jams
void autoModeInterrupt(void); // interrupt handler for auto mode
bool hasPermission(enum User user); // function to check if a user has permission
void moveWindow(struct Button currBtn); // function to move the window
void limitSwitchHandler(int limitSwitch); // function to handle limit switches
void stopWindow(void); // function to stop the window
void delayMS(int ms); // function to introduce delay


void delayMS(int ms){
	int CTR = 0;
	while(CTR < ms*3100) CTR++;
}

int main(void){
	initStructs();
	init();
	
	jamSemaphore = xSemaphoreCreateBinary();
	autoModeSemaphore = xSemaphoreCreateBinary();
	
	xTaskCreate(CheckButtons, "CheckButtons", 100, NULL, 1, NULL);
	xTaskCreate(jamHandler, "jamHandler", 100, NULL, 2, NULL);
	xTaskCreate(autoModeHandler, "autoModeHandler", 100, NULL, 2, NULL);
	
	vTaskStartScheduler();
	return 0;
}

void CheckButtons(void *p){
	
	for( ; ; ){
		
			// Variable to keep track if all the buttons are released
		bool isZero = true;
		
		// Check the state of lock button (button connected to PB4)
		uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 4);
		if (bit == 0) {
			CarWindow.isLocked = true;  // If button is pressed, lock the window
		}
		else if (bit == 1) {
			CarWindow.isLocked = false; // If button is not pressed, unlock the window
		}
		// Loop to check the state of window control buttons (buttons connected to PC4-PC7)
		for(int i = 4; i < 8; i++){
			bit = Get_Bit(GPIO_PORTC_DATA_R, i);
			if(i == 4) {
				bit = Get_Bit(GPIO_PORTD_DATA_R, 2);
			}
			else if(i == 7) {
				bit = Get_Bit(GPIO_PORTD_DATA_R, 3);
			}
			if (bit == 0){
				moveWindow(PortC_Buttons[i-4]); // If a button is pressed, move the window accordingly
				isZero = false;
			}
		}
		// If all buttons are released and autoMode is not enabled, stop the window movement
		if(isZero && !CarWindow.autoMode){
			stopWindow();
		}
		 // Check the state of limit switches (buttons connected to PB0 and PB1)
		bit = Get_Bit(GPIO_PORTB_DATA_R, 0);
		if (bit == 0){
			limitSwitchHandler(0); 
			while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 0);}
		}
		
		bit = Get_Bit(GPIO_PORTB_DATA_R, 1);
		if (bit == 0){
			limitSwitchHandler(1);
			while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 1);}
		}
	}
}

void jamHandler(void *p){
	for(;;) {
		xSemaphoreTake(jamSemaphore, portMAX_DELAY);
		CarWindow.autoMode = false;
		Clear_Bit(GPIO_PORTD_DATA_R, 0);
		Set_Bit(GPIO_PORTD_DATA_R, 1);
		delayMS(500);
		Clear_Bit(GPIO_PORTD_DATA_R, 0);
		Clear_Bit(GPIO_PORTD_DATA_R, 1);
	}
}

void jamInterrupt(void) {
	
	//GPIOIntClear(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
	
	portBASE_TYPE xHigherPriorityTaskWoken = ( ( BaseType_t ) 2 );
	
  xSemaphoreGiveFromISR(jamSemaphore, &xHigherPriorityTaskWoken);

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}



void autoModeHandler(void *p){
	for(;;) {
		xSemaphoreTake(autoModeSemaphore, portMAX_DELAY);
	
		CarWindow.autoMode = ! CarWindow.autoMode;
	}
}

void autoModeInterrupt(void) {
	
//	GPIOIntClear(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);

	portBASE_TYPE xHigherPriorityTaskWoken = ( ( BaseType_t ) 2 );

	xSemaphoreGiveFromISR(autoModeSemaphore, &xHigherPriorityTaskWoken);

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}



bool hasPermission(enum User user){
  if(CarWindow.isLocked && user == passenger)
    return false;
  return true;
}

bool checkAutoUp( void ){
	uint32_t upDriverBit = Get_Bit(GPIO_PORTD_DATA_R, 2); // get value of bit 2 on port D and store it in upDriverBit
	uint32_t upPassengerBit = Get_Bit(GPIO_PORTC_DATA_R, 6);  // get value of bit 6 on port C and store it in upPassengerBit
	if ( upDriverBit == 0 || upPassengerBit == 0 ){
		return true;
	}
	return false;
}
bool checkAutoDown( void ){
	uint32_t downDriverBit = Get_Bit(GPIO_PORTC_DATA_R, 5); // get value of bit 5 on port C and store it in downDriverBit
	uint32_t downPassengerBit = Get_Bit(GPIO_PORTD_DATA_R, 3); // get value of bit 3 on port D and store it in downPassengerBit
	if ( downDriverBit == 0 || downPassengerBit == 0 ){
		return true;
	}
	return false;
}
void moveWindow(struct Button currBtn){
    if (! hasPermission(currBtn.user)){ // if the user does not have permission
        Clear_Bit(GPIO_PORTD_DATA_R, 0); // clear bit 0 on port D
        Clear_Bit(GPIO_PORTD_DATA_R, 1); // clear bit 1 on port D
        return; // exit function
	}
	if (! CarWindow.autoMode){
		if (currBtn.dir == up){
			if (! CarWindow.isFullyClosed){
				Set_Bit(GPIO_PORTD_DATA_R, 0); // set bit 0 on port D
        Clear_Bit(GPIO_PORTD_DATA_R, 1); // clear bit 1 on port D
			}
		}
		else if (currBtn.dir == down){
			if (! CarWindow.isFullyOpened){
				Clear_Bit(GPIO_PORTD_DATA_R, 0);
				Set_Bit(GPIO_PORTD_DATA_R, 1);
			}
		}
	}
	else{
		if (currBtn.dir == up){ //Check if the button direction is up
			//Start of a while loop that runs as long as the window is not fully closed and the auto mode is on.
			while(! CarWindow.isFullyClosed && CarWindow.autoMode){
				uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 0);
				if (bit == 0){
					limitSwitchHandler(0); // If the value of bit is 0, call the limitSwitchHandler() function with the argument 0 to handle the limit switch for the up direction.
					while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 0);}
					continue;
				}
				else if ( checkAutoDown() ){ //Else if the value of checkAutoDown() function is true, set the auto mode of the car window to false and continue with the outer while loop.
					CarWindow.autoMode = false;
					continue;
				}
				else{
					Set_Bit(GPIO_PORTD_DATA_R, 0);
					Clear_Bit(GPIO_PORTD_DATA_R, 1);
				}
			}
		}
		else if (currBtn.dir == down){
			while(! CarWindow.isFullyOpened && CarWindow.autoMode){
				uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 1);
				if (bit == 0){
					limitSwitchHandler(1);
					while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 1);}
					continue;
				}
				else if ( checkAutoUp() ){
					CarWindow.autoMode = false;
					continue;
				}
				else{
					Clear_Bit(GPIO_PORTD_DATA_R, 0);
					Set_Bit(GPIO_PORTD_DATA_R, 1);
				}
			}
		}
	}
}

void stopWindow(void){ //function definition to stop the car window
	Clear_Bit(GPIO_PORTD_DATA_R, 0);
	Clear_Bit(GPIO_PORTD_DATA_R, 1);
}
//function definition to handle limit switches, takes an integer parameter
void limitSwitchHandler(int limitSwitch){  
  if (limitSwitch == 0){
		CarWindow.isFullyClosed = !CarWindow.isFullyClosed;
  }
  else if (limitSwitch == 1){
		CarWindow.isFullyOpened = !CarWindow.isFullyOpened;
  }
	CarWindow.autoMode = false;
}

void initStructs(void){

	//These lines set the initial values of four boolean properties
	CarWindow.isFullyClosed = false;
	CarWindow.isFullyOpened = false;
	CarWindow.isLocked = false;
	CarWindow.autoMode = false;
	// These lines declare four instances of the Button
	static struct Button driverUpButton;
	static struct Button driverDownButton;
	static struct Button passengerUpButton;
	static struct Button passengerDownButton;
	
	//These lines set the user and dir properties of the four Button instances according to the specified values
	driverUpButton.user = driver;
	driverUpButton.dir = up;
	
	driverDownButton.user = driver;
	driverDownButton.dir = down;
	
	passengerUpButton.user = passenger;
	passengerUpButton.dir = up;
	
	passengerDownButton.user = passenger;
	passengerDownButton.dir = down;
	
	PortC_Buttons[0] = driverUpButton;
	PortC_Buttons[1] = driverDownButton;
	PortC_Buttons[2] = passengerUpButton;
	PortC_Buttons[3] = passengerDownButton;
}

void init(void){

	//PORT F SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
	
	//PORT B SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));
	
	//PORT C SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));
	
	//PORT D SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));
	
	//Manual/Auto Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTF_BASE , GPIO_PIN_4 );
	GPIOIntRegister(GPIO_PORTF_BASE, autoModeInterrupt);
	GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_4 );
	Set_Bit(GPIO_PORTF_PUR_R, 4);
	
	//Jam Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_5 );
	GPIOIntRegister(GPIO_PORTB_BASE, jamInterrupt);
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_5, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTB_BASE, GPIO_INT_PIN_5 );
	Set_Bit(GPIO_PORTB_PUR_R, 5);
	
	//Motor Pins Setup
 	GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	
	//Limit Switch Pins Setup
  GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTB_PUR_R, 0);
	Set_Bit(GPIO_PORTB_PUR_R, 1);
	
	//On/Off Switch Pins Setup
  GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_4 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTB_PUR_R, 4);
	
	//Up and Down Pins Setup
	GPIOPinTypeGPIOInput(GPIO_PORTC_BASE , GPIO_PIN_5 | GPIO_PIN_6 );
	GPIOIntTypeSet(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6 , GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTC_PUR_R, 5);
	Set_Bit(GPIO_PORTC_PUR_R, 6);
	
	GPIOPinTypeGPIOInput(GPIO_PORTD_BASE , GPIO_PIN_2 | GPIO_PIN_3 );
	GPIOIntTypeSet(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTD_PUR_R, 2);
	Set_Bit(GPIO_PORTD_PUR_R, 3);
	
	// Enable the Interrupt for PortF & PortB in NVIC
	__asm("CPSIE I");
	IntMasterEnable();
	IntEnable(INT_GPIOF);
	IntEnable(INT_GPIOB);
	IntPrioritySet(INT_GPIOF, 0xE0);
	IntPrioritySet(INT_GPIOB, 0xE0);
}


//////////////
//	Manual/Auto Button
//////////////
// F0

//////////////
//	Up & Down
//////////////
// C4 -> D2 (Now)
// C5
// C6
// C7 -> D3 (Now)

//////////////
//	Limit Switches
//////////////
// B0
// B1

//////////////
//	On/Off Switch

// B4

//////////////
//	Jam Switch
// B5

//////////////
//	Motor Pins
// D0
// D1