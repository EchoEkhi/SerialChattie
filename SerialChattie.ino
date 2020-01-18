#include <LCD5110_Graph.h>
#include <SoftwareSerial.h>
#include <Encoder.h>
#include <TimerOne.h>

LCD5110 myGLCD(8, 9, 10, 11, 12); // backlight is at 13
SoftwareSerial wireless(4, 5);    // RX, TX // 6 is AT settings pin
Encoder Enc(2, 3);

unsigned long int encoffset, backlighttimer, shutdowntimer = 0;
extern uint8_t SmallFont[], MediumNumbers[];
String type;
String message[3];
String devices[3];
String options[6];
bool blinkpowerstate, backlight;
int uioffset, onlinedevicescount;
class Comms
// This class requires a software serial connection called "wireless" to be declared
{
public:
    String devicename;
    String incomingmessage;

    Comms(String deviceName)
    {
        devicename = deviceName;
    }

    // begins the serial connection to the wireless module
    void begin(int baudrate)
    {
        wireless.begin(baudrate);
    }

    // listens on the serial port, use inside of a loop
    void listen()
    {
        if (wireless.available())
        {
            record();
            respond();
        }
    }

    // broadcasts a message (no ack)
    void broadcast(String message)
    {
        // sends the message
        wireless.print(char(10));
        wireless.print(devicename);
        wireless.print(" ");
        wireless.print(message);
        wireless.print(char(13));
    }

    // sends a message (expects ack)
    void send(String target, String command)
    {
        // check if target is itself
        if (target == devicename)
        {
            // bounce it back to inbox and not send it
            setmessage(devicename + " " + target + " " + command);
            return;
        }

        // else, send the message
        for (int i = 0; i < 3; i++)
        {
            // sends the message
            wireless.print(char(10));
            wireless.print(devicename);
            wireless.print(" ");
            wireless.print(target);
            wireless.print(" ");
            wireless.print(command);
            wireless.print(char(13));
            // after that, listen for ack
            if (waitforack())
            {
                return;
            }
        }
        setmessage("! ack timeout");
        return;
    }

    // announces itself and ask other devices to register itself
    void announce()
    {
        // <source> announce // asks nearby devices to respond with their names
        wireless.print(char(10) + devicename + " announce" + char(13));
    }

    void atinit(bool atstate)
    {
        digitalWrite(6, atstate);
    }

    void setmessage(String txt)
    {
        message += char(10) + txt + char(13);
    }

    void nextmessage()
    {
        String txt = "";
        bool issecondmessage = false;
        for (int i = 0; i < message.length(); i++)
        {
            if (issecondmessage)
            {
                txt += message.charAt(i);
            }

            if (message.charAt(i) == 13)
            {
                issecondmessage = true;
            }
        }
        message = txt;
    }

    bool newmsgavailable()
    {
        if (newmessage)
        {
            newmessage = false;
            return true;
        }
        else
        {
            return false;
        }
    }

    bool available()
    {
        return message.length() != 0;
    }

    // returns the first segment and clears the available state
    String read()
    {
        String txt = "";
        for (int i = 0; i < message.length(); i++)
        {
            switch (int(message.charAt(i)))
            {
            case 10:
                break;
            case 13:
                return txt;
            default:
                txt += message.charAt(i);
                break;
            }
        }
    }

    // returns the argument of requestedItem
    // e.g. txt = "hello world", requestedItem = 1, returning string would be "world"
    String pharse(String txt, int requestedItem)
    {
        String response = "";
        int currentItem = 0;
        bool inquotes = false;
        // go through the length of the text, one at a time
        for (int i = 0; i < txt.length(); i++)
        {
            if (txt.charAt(i) == ' ' && !inquotes)
            // checks if it is a space and not in a quote
            {
                // if so, increment item count
                currentItem++;

                // checks if the last item is the one that's requested
                if (currentItem > requestedItem)
                {
                    // if so, return the recorded string
                    return response;
                }
                // if not, then clear buffer and start recording the next segment
                response = "";
            }
            else if (txt.charAt(i) == '"')
            // check if it is a quotemark
            {
                // if so, toggle inquotes
                inquotes = !inquotes;
            }
            else
            {
                // if not, just record it into buffer for future use
                response += String(txt.charAt(i));
            }
        }

        // fallback check for if the requested item is the last item.
        // in this case, the above mechanism would not trigger
        // because there isn't a space at the end of transmission
        if (currentItem == requestedItem)
        {
            return response;
        }
        return "-1";
    }

private:
    char incomingchar;
    String message;
    bool newmessage;

    bool waitforack()
    {
        long unsigned int acktimeout = millis();
        while (millis() - acktimeout < 1000)
        {
            if (wireless.available())
            {
                if (wireless.read() == char(6))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // records incoming messages
    void record()
    {
        long unsigned int timer = millis();
        while (true)
        {
            // wait until a byte comes in
            while (!wireless.available())
            {
                // check if it's timed out
                if (millis() - timer > 1000)
                {
                    // timed out
                    incomingchar = 0;
                    incomingmessage = "";
                    setmessage("! incoming timeout");
                    return;
                }
            }

            // reset timeout timer
            timer = millis();

            // reads the incoming byte
            incomingchar = wireless.read();

            // detect end of transmission
            // filters control bytes, 10 starts message, 13 ends message
            switch (incomingchar)
            {
            case 6:
                // ack byte
                break;
            case 10:
                // transmission starts, clear buffer
                incomingmessage = "";
                break;
            case 13:
                // transmission ends
                setmessage(incomingmessage);
                newmessage = true;
                return;
            default:
                incomingmessage += incomingchar;
                break;
            }
        }
    }

    void respond()
    {
        // check if target of the message is this machine
        if (pharse(incomingmessage, 1) == devicename)
        {
            // ack syntax: 6
            // do not use send(), otherwise there will be a broadcast catastrophe of acks in the air,
            // and the NSA would come knocking at your door "knack" "knack"
            wireless.print(char(6));
        }

        // check if message is a brodacasted query for available devices
        // <source> announce // asks nearby devices to respond with their names
        if (pharse(incomingmessage, 1) == "announce")
        {
            // wait until other devices finish transmitting
            while (wireless.available())
            {
                record();
                respond();
            }
            // <source> <target> online // tells target (everyone, actually) that device is online
            send(pharse(incomingmessage, 0), "online");
        }
    }
};

Comms comms("terminal");

void setup()
{
    myGLCD.InitLCD();
    myGLCD.setFont(SmallFont);
    wireless.begin(9600);
    Serial.begin(9600);
    pinMode(6, OUTPUT);    // wireless AT pin
    digitalWrite(6, HIGH); // default disable AT mode
    // Keeping the power module awake
    Timer1.initialize(5000000);
    Timer1.attachInterrupt(blinkpower);
    comms.broadcast("init");
    comms.announce();
}

void loop()
{
    // serial debugging tool, relays serial (computer) to the typing box
    if (Serial.available())
    {
        type += char(Serial.read());
    }

    // renders messages
    myGLCD.clrScr();
    displayscroll(message[2], 0, 0);
    displayscroll(message[1], 0, 8);
    displayscroll(message[0], 0, 16);

    // handles wireless inputs
    comms.listen();

    // renders the virtual keyboard
    renderkeyboard();

    // checks if a message is available
    if (comms.available())
    {
        scrollmessages(comms.read());
        commandtree(comms.read());
        comms.nextmessage();
    }

    // blinkbacklight background process, blinks backlight when message is received
    digitalWrite(13, (millis() - backlighttimer < 250) ^ backlight);
}

void commandtree(String command)
{
    // checks target of command
    if (comms.pharse(command, 1) == comms.devicename)
    // command directed at this device
    {
        // checks command arguments
        if (comms.pharse(command, 2) == "ping")
        // <source> <target> ping // respond with "pong"
        {
            comms.send(comms.pharse(command, 0), "pong");
            return;
        }
        else if (comms.pharse(command, 2) == "pong")
        // <source> <target> pong // response of "ping", do nothing (alert user?)
        {
            return;
        }
        else if (comms.pharse(command, 2) == "negative")
        // <source> <target> negative // do nothing (alert user?)
        {
            if (comms.pharse(command, 3) == "invalid")
            // <source> <target> negative invalid // do nothing (alert user?)
            {
                return;
            }
        }
        else if (comms.pharse(command, 2) == "online")
        {
            // check if device is already in registry
            for (int i = 0; i < 3; i++)
            {
                if (comms.pharse(command, 0) == devices[i])
                {
                    // device is already registered, no action required
                    return;
                }
            }
            // if not, add device to the registry
            devices[onlinedevicescount] = comms.pharse(command, 0);
            onlinedevicescount++;
        }
        else if (comms.pharse(command, 2) == "ack")
        // <source> <target> ack // do nothing
        {
            return;
        }
        else if (comms.pharse(command, 2) == "show")
        // <source> <target> message <content> // raise a dialogue box
        {
            displaymessage("Remote Message", "From " + comms.pharse(command, 0) + ":", comms.pharse(command, 3));
            return;
        }
        else if (comms.pharse(command, 2) == "set")
        // <source> <target> set <index> "<content>" // set the options array
        {
            int optnum = comms.pharse(command, 3).toInt();
            options[optnum] = comms.pharse(command, 4);
            return;
        }
        else if (comms.pharse(command, 2) == "menu")
        // <source> <target> menu <optcount> "<title>" // trigger a menu and send the response
        {
            // reset backlight status
            digitalWrite(13, backlight);
            int optnum = comms.pharse(command, 3).toInt();
            String title = comms.pharse(command, 4);
            int choice = menu(optnum, title, options);
            // <source> <target> respond <value>
            comms.send(comms.pharse(command, 0), "respond " + String(choice));
            return;
        }
        else if (comms.pharse(command, 2) == "selnum")
        // <source> <target> selnum // trigger a selnum and send the response
        {
            // reset backlight status
            digitalWrite(13, backlight);
            int choice = selnum(options[0], options[1].toInt(), options[2].toInt(), options[3].toInt());
            // <source> <target> respond <value>
            comms.send(comms.pharse(command, 0), "respond " + String(choice));
            return;
        }
        else
        // no command matches // respond with "negative invalid"
        {
            comms.send(comms.pharse(command, 0), "negative invalid");
            return;
        }
    }
}

int interize(String input)
{
    return int(char(input[0]) - 48);
}

char list(int index)
{
    if (index >= 0 && index <= 36)
    {
        return "abcdefghijklmnopqrstuvwxyz\"0123456789"[index];
    }
    else
    {
        return 0;
    }
}

void scrollmessages(String newstr)
{
    message[2] = message[1];
    message[1] = message[0];
    message[0] = newstr;
    blinkbacklight();
}

void blinkbacklight()
{
    backlighttimer = millis();
}

void renderkeyboard()
{
    myGLCD.print(type, 84 - type.length() * 6, 24);

    myGLCD.drawLine(0, 31, 84, 31);
    myGLCD.drawLine(0, 23, 84, 23);

    uioffset = enc() - 6;
    if (uioffset > 29)
    {
        uioffset = 29;
    }
    for (int i = 0; i < 12; i++)
    {
        myGLCD.invertText(enc() == uioffset + i && enc() <= 36);
        myGLCD.print(String(list(uioffset + i)), i * 6, 32);
    }

    myGLCD.invertText(enc() == 37);
    myGLCD.print(" ", 72, 32);
    myGLCD.invertText(enc() == 38);
    myGLCD.print("*", 78, 32);
    myGLCD.invertText(enc() == 39);
    myGLCD.print("Undo", 0, 40);
    myGLCD.invertText(enc() == 40);
    myGLCD.print("Confirm", 42, 40);
    myGLCD.invertText(false);
    myGLCD.update();

    // handles input
    ouflowpreventer(40, 0, 0, 40);
    if (clicked(true))
    {
        while (debounce())
            ;
        switch (enc())
        {
        case 37: // space( )
            type += " ";
            break;
        case 38: // menu(*)
            menutree();
        case 39: // backspace (undo)
            type.remove(type.length() - 1);
            break;
        case 40: // send (confirm)
            scrollmessages(type);
            wireless.print(char(10));
            wireless.print(type);
            wireless.print(char(13));
            type = "";
            break;
        default:
            type += char(list(enc()));
            break;
        }
    }
}

void ouflowpreventer(int maxi, int maxidef, int mini, int minidef)
{
    if (int(enc()) > maxi)
    {
        encoffset = Enc.read() * -1 - maxidef;
    }
    if (int(enc()) < mini)
    {
        encoffset = Enc.read() * -1 - minidef;
    }
}

long int enc()
{
    return int(Enc.read() * -1 - encoffset);
}

bool clicked(bool inverter)
{
    return analogRead(A1) > 100 ^ inverter;
}

bool debounce()
{
    unsigned long int i = millis();
    while (millis() - i < 50)
    {
        if (clicked(true))
        {
            i = millis();
        }
    }
    return false;
}

void blinkpower()
{
    // change the state of the power pin to keep the chip awake
    if (shutdowntimer != 0 && millis() > shutdowntimer)
    {
        digitalWrite(7, LOW);
        scrollmessages(F("shutdown initiated"));
    }
    else
    {
        blinkpowerstate = !blinkpowerstate;
        digitalWrite(7, blinkpowerstate);
    }
}

void menutree()
{
    options[0] = F("Firmware Info");
    options[1] = F("Backlight");
    options[2] = F("Scan Devices");
    options[3] = F("Access Devices");
    options[4] = F("Shutdown");
    switch (menu(5, "Main menu", options))
    {
    case 1:
        // Firmware information
        // draw splash screen
        myGLCD.clrScr();
        myGLCD.drawRect(0, 0, 83, 47);
        for (int i = 0; i < 48; i += 4)
        {
            myGLCD.drawLine(0, i, i * 1.75, 47);
            myGLCD.update();
        }
        for (int i = 0; i < 48; i += 4)
        {
            myGLCD.drawLine(83, 47 - i, 83 - (i * 1.75), 0);
            myGLCD.update();
        }
        delay(500);
        myGLCD.print(F("Terminal"), CENTER, 0);
        myGLCD.print(F("Firmware: v0.8"), CENTER, 8);
        myGLCD.print(F("SerialChattie"), CENTER, 16);
        myGLCD.print(F("Beta Release"), CENTER, 24);
        myGLCD.print(F("Compiled on"), CENTER, 32);
        myGLCD.print(__DATE__, CENTER, 40);
        myGLCD.update();
        while (clicked(false))
            ;
        while (debounce())
            ;
        break;
    case 2: // backlight
        backlight = !backlight;
        break;
    case 3: // get devices list
        // clear all the existing devices
        for (int i = 0; i < 3; i++)
        {
            devices[i] = "";
        }
        onlinedevicescount = 0;
        comms.announce();
        break;
    case 5: // schedule shutdown. For some reason, it doesn't work when put under case 4
        if (shutdowntimer == 0)
        {
            int shutdowntime = selnum(F("Schedule Shutdown"), 0, 60, -1);
            if (shutdowntime != -1)
            {
                shutdowntimer = 60000 * shutdowntime + millis();
                scrollmessages(F("Shutdown Scheduled"));
            }
        }
        else
        {
            shutdowntimer = 0;
            scrollmessages(F("Shutdown Cancelled"));
        }
        break;
    case 4: // launch devices' menu
        int choice = menu(onlinedevicescount, "Devices", devices);
        if (choice != 0)
        {
            comms.send(devices[choice - 1], "query");
        }
        break;
    }
}

int menu(int optnum, String title, String opts[])
{
    int renderoffset = 0;
    digitalWrite(13, backlight);
    encoffset = Enc.read() - 1;
    while (clicked(false))
    {
        myGLCD.clrScr();
        ouflowpreventer(optnum, optnum, 0, 0);

        if (enc() - renderoffset < 1)
        {
            renderoffset--;
        }
        else if (enc() - renderoffset > 4)
        {
            renderoffset++;
        }

        if (renderoffset < 0)
        {
            renderoffset = 0;
        }
        else if (renderoffset > optnum)
        {
            renderoffset = optnum;
        }

        for (int i = 0; i + renderoffset < optnum; i++)
        { // render options
            myGLCD.print(opts[i + renderoffset], 6, i * 8 + 11);
        }
        if (enc() == 0)
        { // top title bar, change into return when selected
            myGLCD.drawLine(0, 0, 84, 0);
            myGLCD.invertText(true);
            myGLCD.print(F("     HOME     "), CENTER, 1);
            myGLCD.print(F("*"), 0, 1);
            myGLCD.invertText(false);
        }
        else
        { // default
            displayscroll(title, CENTER, 1);
            myGLCD.drawLine(0, 9, 84, 9);
            myGLCD.print(">", 0, int(enc() - renderoffset) * 8 + 3);
        }
        myGLCD.update();
    }
    while (debounce())
        ;
    return enc();
    myGLCD.clrScr();
}

void displayscroll(String text, int x, int y)
{
    if (text.length() > 14)
    {
        myGLCD.print(text, -1 * millis() / 40 % ((text.length() + 14) * 6) - text.length() * 6, y);
    }
    else
    {
        myGLCD.print(text, x, y);
    }
}

void displaymessage(String title, String text1, String text2)
{
    myGLCD.clrScr();
    myGLCD.invertText(true);
    displayscroll(title, CENTER, 0);
    myGLCD.invertText(false);
    myGLCD.drawLine(0, 8, 84, 8);
    displayscroll(text1, 0, 9);
    displayscroll(text2, CENTER, 17);
    myGLCD.invertText(true);
    myGLCD.print("Confirm", 40, 42);
    myGLCD.invertText(false);
    myGLCD.update();
    while (clicked(false))
        ;
    while (debounce())
        ;
}

void textbox(String title, String text1)
{
    while (clicked(false))
    {
        myGLCD.clrScr();
        displayscroll(title, CENTER, 0);
        myGLCD.drawLine(0, 8, 84, 8);
        displayscroll(text1, CENTER, 9);
        if (type.length() > 12)
        {
            myGLCD.print(type, (type.length() - 12) * -6, 24);
        }
        else
        {
            myGLCD.print(type, 0, 24);
        }

        myGLCD.drawLine(0, 31, 84, 31);

        if (enc() > uioffset + 11)
        {
            uioffset++;
        }
        else if (enc() < uioffset)
        {
            uioffset--;
        }

        if (uioffset < 0)
        {
            uioffset = 0;
        }
        else if (uioffset > 24)
        {
            uioffset = 24;
        }

        for (int i = 0; i < 12; i++)
        {
            myGLCD.invertText(enc() == uioffset + i);
            myGLCD.print(String(list(uioffset + i)), i * 6, 32);
        }

        myGLCD.invertText(enc() == 36);
        myGLCD.print(" ", 72, 32);
        myGLCD.invertText(enc() == 37);
        myGLCD.print("*", 78, 32);
        myGLCD.invertText(enc() == 38);
        myGLCD.print("Undo", 0, 40);
        myGLCD.invertText(enc() == 39);
        myGLCD.print("Confirm", 40, 42);
        myGLCD.invertText(false);
        myGLCD.update();
        ouflowpreventer(39, 0, 0, 39);
    }

    switch (enc())
    {
    case 36:
        if (type.length() < 14)
        {
            type += " ";
        }
        break;
    case 37:
        menutree();
        break;
    case 38:
        type.remove(type.length() - 1);
        break;
    case 39:
        scrollmessages(type);
        type = "";
        break;
    default:
        type += char(list(enc()));
        break;
    }
    while (debounce())
        ;
}

int selnum(String title, int mini, int maxi, int defaultint)
{
    encoffset = Enc.read() - 1 - defaultint + mini; //reset
    while (clicked(false))
    {
        myGLCD.clrScr();
        if (int(Enc.read() - 1 - encoffset + mini) < mini)
        { // breach minimum
            myGLCD.invertText(true);
            myGLCD.print(F("    RETURN    "), CENTER, 1);
            myGLCD.invertText(false);
            myGLCD.drawLine(0, 0, 84, 0);
            myGLCD.setFont(MediumNumbers);
            myGLCD.print(String(mini), CENTER, 15);
            myGLCD.setFont(SmallFont);
            if (int(Enc.read() - 1 - encoffset + mini) < mini - 1)
            {
                encoffset = Enc.read();
            }
        }
        else if (int(Enc.read() - 1 - encoffset + mini) > maxi)
        { // breach maximum
            displayscroll(title, CENTER, 1);
            encoffset = Enc.read() - 1 + mini - maxi;
            myGLCD.setFont(MediumNumbers);
            myGLCD.print(maxi, CENTER, 15);
            myGLCD.setFont(SmallFont);
        }
        else
        { // norm
            myGLCD.setFont(MediumNumbers);
            myGLCD.print(String(Enc.read() - 1 - encoffset + mini), CENTER, 15);
            myGLCD.setFont(SmallFont);
            displayscroll(title, CENTER, 1);
        }
        myGLCD.drawLine(0, 9, 84, 9); //UI
        myGLCD.update();
    }

    while (debounce())
        ;

    if (int(Enc.read() - 1 - encoffset + mini) < mini)
    {
        return defaultint; //default because it underflowed to RETURN
    }
    if (int(Enc.read() - 1 - encoffset + mini) > maxi)
    {
        return maxi; //max overflow
    }
    else
    {
        return Enc.read() - 1 - encoffset + mini; //norm
    }
}
