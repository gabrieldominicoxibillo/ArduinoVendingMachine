
// POPCARD INTERFACE
// CREATED BY AvBrand.com, April 2011, modified into latest syntax May 2023

#include <SPI.h>
#include <Ethernet.h>
#include <LiquidCrystal.h>
#include "pitches.h"
#include <Time.h> 

const int rs = 6, en = 5, d4 = 4, d5 = 3, d6 = 2, d7 = 13;
#define BACKLIGHT_PIN     13
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);



#define PIN_RFID_RX      0
#define PIN_RFID_RESET   2
#define PIN_LCD_RS       3
#define PIN_LCD_ENABLE   4
#define PIN_LCD_D4       5

#define PIN_LCD_D5       6
#define PIN_LCD_D6       7
#define PIN_LCD_D7       8
#define PIN_VEND_RELAY   9  // pulse twice to add $1 to machine
#define PIN_BILL_ENABLE  A0 // when HIGH, the vending machine can accept money.
#define PIN_BUTTON       A1 // press to add money

#define PIN_BUZZER       A2 // the piezo buzzer
#define PIN_RESET_NET    A3 // network module reset

//
#define MODE_STARTUP     0 // Initial mode
#define MODE_CONNECT     1 // Connecting

#define MODE_UNAVAIL     2 // Unavailable / Not connected
#define MODE_IDLE        3 // Idle, Not doing anything
#define MODE_WAIT_NAME   4 // Tag was scanned, waiting for the name of the person
#define MODE_WAIT_CREDIT 5 // Name received, now waiting for the credit amount to show up.
#define MODE_READY       6 // Ready for the user to insert money with the button

#define MODE_ERROR       7 // Something went wrong.
#define MODE_WAIT_BUY    8 // Waiting for BUY confirmation.
#define MODE_NO_MONEY    9 // No money
#define MODE_NO_MONEY2  10 // No Money message #2
#define MODE_NO_MORE    11 // Can't put any money in right now, due to INHIBIT

#define MODE_UNKNOWN    12 // Unknown card was scanned



byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 0, 2);//the IP address for the shield:
IPAddress gateway( 192, 168, 0, 1 );
IPAddress subnet( 255, 255, 255, 0 );
IPAddress timeServer(132, 163, 4, 102); // time-a.timefreq.bldrdoc.gov
char emailserver[] = "192.168.0.3";  // smtp mail server  (mine.com)
char emaildomain[] ="towereng.ca";
const int timeZone = -5;     // Central European Time
EthernetServer server(80);       // create a server at port 80

// the media access control (ethernet hardware) address for the shield:
//byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };   
//byte ip[] = { 192, 168, 1, 10 };     //the IP address for the shield:
//byte server[] = { 192, 168, 1, 11 }; // The server to connect to.

char data[50]; // Data buffer for TCP comms
byte dataPos = 0; // Position in data buffer.
char myName[20];  // Current user's name
float myCredit = 0; // Current credit
char serRead[20]; // Serial input buffer
byte serPos = 0; // Position in input buffer.
byte CURR_MODE = 0; // Current operation mode.
long modeStart = 0; // When did we enter this mode?
byte buyPrompt = 0; // Have we shown the "push to buy" prompt yet?
long lastPing = 0; // When did we receive the last ping?


EthernetUDP Udp;
EthernetClient client;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t prevDisplay = 0; // when the digital clock was displayed
long resettimer = millis();
long lastreset = 0;
  








void setup()
{
  buzz(NOTE_C4, 8); // Startup sound

  delay(500);
  
  // Start up the ethernet interface.
  pinMode(PIN_RESET_NET, OUTPUT);

  digitalWrite(PIN_RESET_NET, LOW);
  
  // Startup sound again
  buzz(NOTE_D4, 8);

  
  // Set up the ethernet and serial.
  Ethernet.begin(mac, ip);
  Serial.begin(9600);

  // Give the ethernet time to start up.
  delay(500);  

  // Startup sound position 3
  buzz(NOTE_E4, 8);

  // Set up the LCD module
  lcd.begin(16, 2);  
  lcd.print("Startup");

  buzz(NOTE_G4, 8);

  // Set up my pins.
  pinMode(PIN_VEND_RELAY, OUTPUT);

  pinMode(PIN_RFID_RESET, OUTPUT);
  pinMode(PIN_BILL_ENABLE, INPUT);

  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  
  // Play the "One Up" sound to start 
  soundOneUp();
  
  // Turn on the RFID module
  digitalWrite(PIN_RFID_RESET, HIGH);

  // Set the initial mode and make the connection.
  setMode(MODE_STARTUP);
  connectToServer();
}

void loop () 
{
  
  // Main program loop.
  
  long mil = millis();

  // Did the timer loop around?   Prevent errors or hangs by the timer looping back to 0
  if (lastPing > mil) lastPing = mil;

  if (modeStart > mil) modeStart = mil;
  
  // Is my TCP connection still alive?

 // if ((!client.connected() && lastPing == 0) || mil - lastPing > 15000)

  {
    // Reconnect.
    connectToServer(); 
  }

  // If we are connected, then the rest of the logic can run.

  switch (CURR_MODE)
  {
    case MODE_IDLE:
      checkRFID(); // See if a tag has been scanned.

      break;
  
    case MODE_READY: // Only allow us to be in this mode for 8 seconds
      if (mil - modeStart > 8000)

      {
        setMode(MODE_IDLE);  // Jump out of this mode and go back to being ready to scan cards
      }
      
      if (mil - modeStart > 4000 && buyPrompt == 0) // Display buy prompt after 4 seconds

      {
        buyPrompt = 1;
        //         1234567890123456
        lcd.clear();

        lcd.print("Push Button");
        lcd.setCursor(0,1);

        lcd.print("to spend $1.00");
        break;
        
      }
      
      // Check the button.

      if (digitalRead(PIN_BUTTON) == HIGH)
      {

        // Are we allowed to add money?
        if (digitalRead(PIN_BILL_ENABLE) == HIGH)

        {
          setMode(MODE_WAIT_BUY);
          sendCommand('B', "");

        } else {
          // We've already reached the max amount, or maybe the pop machine isn't ready for us.
          setMode(MODE_NO_MORE); 
        }

      }
      
      break;
      
    case MODE_WAIT_NAME:  // Timeout for these modes
    case MODE_WAIT_CREDIT:

    case MODE_WAIT_BUY:
      if (mil - modeStart > 8000)

        setMode(MODE_ERROR);  // Jump out of this mode and go back to being ready to scan cards
      break;
      
    case MODE_ERROR:

    case MODE_NO_MONEY2:
    case MODE_UNKNOWN:
      if (mil - modeStart > 2000)

        setMode(MODE_IDLE);  // Jump out of this mode and go back to being ready to scan cards
      break;

    case MODE_NO_MORE:

      if (mil - modeStart > 2000)
        setMode(MODE_READY);  // Go back to the credit display.

      break;

    case MODE_NO_MONEY:
      if (mil - modeStart > 2000)

        setMode(MODE_NO_MONEY2);  // Show the 2nd 'No Money' message
      break;
      
  }

 // if (client.connected())

  {
    // See if the client has sent us any data.
    checkNetData();
  }
}

void setMode(byte b) // Change to this mode.
{
  CURR_MODE = b;

  modeStart = millis(); // When did we enter this mode?

  lcd.clear();

  switch(b)
  {
  case MODE_STARTUP:
    //         1234567890123456

    lcd.print("Starting Up...");
    break;

  case MODE_CONNECT:

    lcd.print("Connecting...");
    break;

  case MODE_UNAVAIL:

    lcd.print("Cards Not Avail.");
    lcd.setCursor(0,1);

    lcd.print("Use Coins Below");
    
    // Turn off the RFID module.
    digitalWrite(PIN_RFID_RESET, LOW);    
    break;

  case MODE_IDLE:
    //         1234567890123456
    lcd.print("   Scan your    ");

    lcd.setCursor(0,1);
    lcd.print("  PopCARD now!  ");

    // Turn on the RFID module.
    digitalWrite(PIN_RFID_RESET, HIGH);
    break;

  case MODE_WAIT_NAME:
    lcd.print("Please wait...");
    break;

  case MODE_WAIT_CREDIT:
    lcd.setCursor(0,1);

    lcd.print("Please wait...");
    break;

  case MODE_READY:

    buyPrompt = 0;
    lcd.print(myName);
    showMoney();

    break;

  case MODE_ERROR:
    lcd.print("An error occured.");

    break;

  case MODE_WAIT_BUY:
    lcd.print("Adding Credit, ");

    lcd.setCursor(0,1);
    lcd.print("Please Wait!");

    break;


  case MODE_NO_MONEY:
    //         1234567890123456
    lcd.print("Not Enough Funds");

    showMoney();
    break;

  case MODE_NO_MORE:

    //         1234567890123456
    lcd.print("Max Amount");
    lcd.setCursor(0,1);

    lcd.print("Reached.");
    break;
    

  case MODE_NO_MONEY2:

    //         1234567890123456
    lcd.print("See Alex");
    lcd.setCursor(0,1);

    lcd.print("to buy credit.");
    break;


  case MODE_UNKNOWN:

    //         1234567890123456
    lcd.print("Invalid or");
    lcd.setCursor(0,1);

    lcd.print("Unknown Card");
    break;

  } 

}

void showMoney()
{
    lcd.setCursor(0,1);

    //         1234567890123456
    //         Credit: $XX.XX
    lcd.print("Credit: $");
    lcd.print(myCredit, DEC);

    lcd.setCursor(myCredit < 10 ? 13 : 14, 1);

    lcd.print("   ");
  
}

void connectToServer()

{
  lastPing = millis();
  setMode(MODE_CONNECT);

  // Reset the network module before all connections.
  buzz(NOTE_C4, 8);
  
  digitalWrite(PIN_RESET_NET, LOW);

  delay(100);
  digitalWrite(PIN_RESET_NET, HIGH);

  delay(500);
  Ethernet.begin(mac, ip);

  delay(1000);

 // client.stop(); // Close the connection

  if (client.connect()) 
  {
    setMode(MODE_IDLE);

  } 
  else 
  {
    setMode(MODE_UNAVAIL);
  }  

}


void checkNetData()
{
  // See if there is data available on the network interface.
  if (client.available() > 0)

  {
    byte d = client.read();
    switch (d)

    {
    case 0x02: // Start of transmission
      dataPos = 0;

      break;
    case 0x03: // End of transmission
      data[dataPos] = 0; // Blank out the end.

      readCommand();
      break;
    default: // Data byte. Put it in the buffer.
      data[dataPos] = d;

      dataPos++;      
    }

  }
}

void readCommand() // Parse an input command from the network interface.

{
  // First byte should be the command.
  char dd[20];
  // Copy the data.
  for (byte i = 1; i <= dataPos && i < 21; i++)

    dd[i-1] = data[i];

  switch (data[0])
  {
    case 'P': // Ping

      lastPing = millis();
      break;
    case 'N': // Name has arrived. Copy into MyName

      if (CURR_MODE == MODE_WAIT_NAME)
      {
        for (byte i = 1; i <= dataPos && i < 21; i++)

          myName[i-1] = data[i];
  
        setMode(MODE_WAIT_CREDIT);

      }
      break;

    case 'C':  // Credit amount has arrived
      if (CURR_MODE == MODE_WAIT_CREDIT)

      {
        myCredit = atof(dd);
        setMode(MODE_READY);

      }
      break;
  
    case 'Y': // Yes, you may buy.
      if (CURR_MODE == MODE_WAIT_BUY)

      {
        myCredit = atof(dd); // Store the new credit amount
  
        soundOneUp();       // Play the credit noise

        addCredit();       // Add $1 to the vending machine.
          
        delay(700); // Provide enough time for the pop machine to register the credit.
        
        setMode(MODE_READY);

      }
      break;
  
    case 'X': // Not enough money.
      if (CURR_MODE == MODE_WAIT_BUY)

      {
         // Play a sad sound.
         buzz(NOTE_G5, 8);
         buzz(NOTE_G4, 8);

         buzz(NOTE_G3, 8);
        
          // Show the no money message.
         setMode(MODE_NO_MONEY);

      }
      break;
  
    case 'R': // Unknown card.
      setMode(MODE_UNKNOWN);

      break;


  }
}

void sendCommand(byte cmd, char cData[]) // Send a command to the host.

{
  // Send a command back to the server.
  client.write(0x02);
  client.write(cmd);

  client.print(cData);
  client.write(0x03);

}

void checkRFID()
{
  // Check the serial port for data from the RFID module.
  if (Serial.available() > 0)

  {
    byte d = Serial.read();
    switch (d)

    {
    case 0x02: // Start of transmission
      serPos = 0;

      break;
    case 0x03: // End of transmission
      readTag();

      break;
    default: // Add to buffer.
      serRead[serPos] = d;

      serPos++;      
    }
  }
}

void readTag() // Parse the data from a card.

{
  serRead[10] = 0; 
  Serial.print("Card Read:");

  Serial.println(serRead);

  setMode(MODE_WAIT_NAME);

  
  // Make a beep
  buzz(NOTE_B4, 8);
  buzz(NOTE_E5, 4);

  // Ask the server who this is.
  sendCommand('S', serRead);

  // Turn off the RFID module.
  digitalWrite(PIN_RFID_RESET, LOW);

}

void addCredit()
{
  // Pulsing the 'VEND NO' and 'VEND COM' lines on the vendor
  // twice tells it that $1 has been inserted.
  
  digitalWrite(PIN_VEND_RELAY, HIGH);

  delay(150);
  digitalWrite(PIN_VEND_RELAY, LOW);

  delay(400);
  digitalWrite(PIN_VEND_RELAY, HIGH);

  delay(150);
  digitalWrite(PIN_VEND_RELAY, LOW);

  
}

void buzz(long note, long dTime)

{
    // Play a tone.
    tone(PIN_BUZZER, note, 1000 / dTime);

    delay(1000/dTime);
    noTone(PIN_BUZZER);
}

void soundOneUp()
{
    // Make the one-up sound.
    tone(PIN_BUZZER, NOTE_E5, 125);

    delay(125);
    tone(PIN_BUZZER, NOTE_G5, 125);

    delay(125);
    tone(PIN_BUZZER, NOTE_E6, 125);

    delay(125);
    tone(PIN_BUZZER, NOTE_C6, 125);

    delay(125);
    tone(PIN_BUZZER, NOTE_D6, 125);

    delay(125);
    tone(PIN_BUZZER, NOTE_G6, 125);

    delay(125);
    noTone(PIN_BUZZER);
}
