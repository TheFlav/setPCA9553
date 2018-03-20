#include <stdio.h>
#include <fcntl.h>     // open
#include <inttypes.h>  // uint8_t, etc
#include <linux/i2c-dev.h> // I2C bus definitions
#include <stdlib.h>

#include <argp.h>

int I2CFile;
uint8_t writeBuf[0xFF];      // Buffer to store the 3 bytes that we write to the I2C device
uint8_t readBuf[0xFF];       // Buffer to store the data read from the I2C device

/* Program documentation. */
static char doc[] =
"Set PCA9553 via i2c, always use -a, only use one of -l, -p, or -s";

/* A description of the arguments we accept. */
static char args_doc[] = "VALUE";

/* The options we understand. */
static struct argp_option options[] = {
    {"address",  'a', "0xXX",                  0,  "i2c Address of PCA9553" },
    {"i2cbus",   'y', "[0|1]",                 0,  "use /dev/i2c-0 or /dev/i2c-1 (hint: see i2cdetect command)" },
    {"led0",     'l', "[ON|Z|PWM0|PWM1]",      0,  "set led0 to ON, High-Impedence, PWM0, or PWM1" },
    {"led1",     'm', "[ON|Z|PWM0|PWM1]",      0,  "set led1 to ON, High-Impedence, PWM0, or PWM1" },
    {"led2",     'n', "[ON|Z|PWM0|PWM1]",      0,  "set led2 to ON, High-Impedence, PWM0, or PWM1" },
    {"led3",     'o', "[ON|Z|PWM0|PWM1]",      0,  "set led3 to ON, High-Impedence, PWM0, or PWM1" },
    {"pwm0",     'p', "0xXX",                  0,  "set pwm0 to 0xXX" },
    {"pwm1",     'q', "0xXX",                  0,  "set pwm1 to 0xXX" },
    {"psc0",     's', "0xXX",                  0,  "set psc0 to 0xXX" },
    {"psc1",     't', "0xXX",                  0,  "set psc1 to 0xXX" },
    { 0 }
};

/* Used by main to communicate with parse_opt. */
struct arguments
{
    uint8_t PCA_i2c_address;
    uint8_t i2cbus;
    uint8_t pwm0;
    uint8_t pwm1;
    uint8_t psc0;
    uint8_t psc1;
    
    uint8_t pwm0_inuse;
    uint8_t pwm1_inuse;
    uint8_t psc0_inuse;
    uint8_t psc1_inuse;
    
    char* led0_str;
    char* led1_str;
    char* led2_str;
    char* led3_str;
    //    char* setting_str;        //the value to set the configuration to
    //    uint8_t register_already_chosen;
};

struct PCA_regs
{
    uint8_t INPUT;
    uint8_t PSC0;
    uint8_t PWM0;
    uint8_t PSC1;
    uint8_t PWM1;
    uint8_t LS0;
} curr_regs;

/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
    struct arguments *arguments = state->input;
    
    /*printf("key=%c\n", key);
     if(arg)
     printf("arg=%s\n", arg);*/
    
    switch (key)
    {
        case 'a':
            arguments->PCA_i2c_address = strtoul(arg, NULL, 16);
            break;
        case 'y':
            arguments->i2cbus = strtoul(arg, NULL, 10);
            if(arguments->i2cbus > 1)
                argp_usage (state);
            break;
        case 'l':
            arguments->led0_str = arg;
            break;
        case 'm':
            arguments->led1_str = arg;
            break;
        case 'n':
            arguments->led2_str = arg;
            break;
        case 'o':
            arguments->led3_str = arg;
            break;
        case 'p':
            arguments->pwm0 = strtoul(arg, NULL, 16);
            arguments->pwm0_inuse = 1;
            //printf("PWM0: 0x%02X\n", arguments->pwm0);
            break;
        case 'q':
            arguments->pwm1 = strtoul(arg, NULL, 16);
            arguments->pwm1_inuse = 1;
            //printf("PWM1: 0x%02X\n", arguments->pwm1);
            break;
        case 's':
            arguments->psc0 = strtoul(arg, NULL, 16);
            arguments->psc0_inuse = 1;
            //printf("PSC0: 0x%02X\n", arguments->psc0);
            break;
        case 't':
            arguments->psc1 = strtoul(arg, NULL, 16);
            arguments->psc1_inuse = 1;
            //printf("PSC1: 0x%02X\n", arguments->psc1);
            break;
            
        case ARGP_KEY_ARG:
            if (state->arg_num >= 0)
            /* Too many arguments. */
                argp_usage (state);
            
            //arguments->setting_str = arg;
            
            break;
            
        case ARGP_KEY_END:
            if (state->arg_num < 0)
            /* Not enough arguments. */
                argp_usage (state);
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

uint8_t generate_new_ls0(uint8_t old_ls0, char *led_str, uint8_t led_num)
{
    
    if(led_num > 3)
        return old_ls0;
    
    /*
     00 = output is set LOW (LED on)
     01 = output is set high-impedance (LED off; default)
     10 = output blinks at PWM0 rate
     11 = output blinks at PWM1 rate
     */
    
    uint8_t ls_val=0xFF;
    if(strcmp(led_str, "PWM0") == 0)
        ls_val = 0b10;
    else if(strcmp(led_str, "PWM1") == 0)
        ls_val = 0b11;
    else if(strcmp(led_str, "ON") == 0)
        ls_val = 0b00;
    else if(strcmp(led_str, "Z") == 0)
        ls_val = 0b01;
    else
    {
        printf("Invalid led string for LED%d\n", led_num);
        exit(1);
    }
    
    //printf("LED%d being set to %d\n", led_num, ls_val);
    
    return (old_ls0 & (~(0x03 << (led_num*2)))) | (ls_val << (led_num*2));
}

void get_curr_regs(void)
{
    writeBuf[0] = 0x10;   //set auto-increment for reading all registers
    write(I2CFile, writeBuf, 1);
    
    read(I2CFile, readBuf, 6);
    
    curr_regs.PSC0  = readBuf[1];
    curr_regs.PWM0  = readBuf[2];
    curr_regs.PSC1  = readBuf[3];
    curr_regs.PWM1  = readBuf[4];
    curr_regs.LS0   = readBuf[5];
    
    writeBuf[0] = 0x00;   //don't set auto-increment for reading input port
    write(I2CFile, writeBuf, 1);
    
    read(I2CFile, readBuf, 1);
    
    curr_regs.INPUT = readBuf[0];
}

void print_curr_regs(void)
{
    printf("  INPUT  0x%02X (read-only)\n", curr_regs.INPUT);
    printf("  PSC0   0x%02X (use -s to set)\n", curr_regs.PSC0);
    printf("  PWM0   0x%02X (use -p to set)\n", curr_regs.PWM0);
    printf("  PSC1   0x%02X (use -t to set)\n", curr_regs.PSC1);
    printf("  PWM1   0x%02X (use -q to set)\n", curr_regs.PWM1);
    printf("  LS0    0x%02X\n", curr_regs.LS0);
    printf("    LED0 %4s (use -l to set)\n", curr_regs.LS0 & 0b10 ? (curr_regs.LS0 & 0b01 ? "PWM1" : "PWM0") : (curr_regs.LS0 & 0b01 ? "Z" : "ON"));
    printf("    LED1 %4s (use -m to set)\n", curr_regs.LS0>>2 & 0b10 ? (curr_regs.LS0>>2 & 0b01 ? "PWM1" : "PWM0") : (curr_regs.LS0>>2 & 0b01 ? "Z" : "ON"));
    printf("    LED2 %4s (use -n to set)\n", curr_regs.LS0>>4 & 0b10 ? (curr_regs.LS0>>4 & 0b01 ? "PWM1" : "PWM0") : (curr_regs.LS0>>4 & 0b01 ? "Z" : "ON"));
    printf("    LED3 %4s (use -o to set)\n", curr_regs.LS0>>6 & 0b10 ? (curr_regs.LS0>>6 & 0b01 ? "PWM1" : "PWM0") : (curr_regs.LS0>>6 & 0b01 ? "Z" : "ON"));
}

int main (int argc, char **argv)
{
    struct arguments arguments;
    
    /* Default values. */
    arguments.PCA_i2c_address = 0x62;   // Address of our device on the I2C bus
    arguments.pwm0_inuse = 0;
    arguments.pwm1_inuse = 0;
    arguments.psc0_inuse = 0;
    arguments.psc1_inuse = 0;
    arguments.i2cbus = 1;
    //arguments.setting_str = NULL;
    arguments.led0_str = NULL;
    arguments.led1_str = NULL;
    arguments.led2_str = NULL;
    arguments.led3_str = NULL;
    
    /* Parse our arguments; every option seen by parse_opt will
     be reflected in arguments. */
    argp_parse (&argp, argc, argv, 0, 0, &arguments);
    
    /*printf ("SETTING = %s\nADDR = 0x%02X\n"
     "PWM = %d\nPSC = %d\nLED = %d\n",
     arguments.setting_str ? arguments.setting_str : "",
     arguments.PCA_i2c_address,
     arguments.pwm,
     arguments.psc,
     arguments.led);*/
    
    
    if(arguments.i2cbus == 1)
        I2CFile = open("/dev/i2c-1", O_RDWR);     // Open the I2C device
    else
        I2CFile = open("/dev/i2c-0", O_RDWR);     // Open the I2C device
    
    ioctl(I2CFile, I2C_SLAVE, arguments.PCA_i2c_address);   // Specify the address of the I2C Slave to communicate with
    
    printf("Current Register Status:\n");
    get_curr_regs();
    print_curr_regs();
    
    //    printf("  LS0   0x%02X (use -l,-m,-n,-o to set)\n", curr_regs.LS0);
    
    uint8_t changes_made = 0;
    if(arguments.psc0_inuse)
    {
        printf("Writing PSC0 value of 0x%02X\n", arguments.psc0);
        writeBuf[0] = 0b00000001;           //no increment
        writeBuf[1] = arguments.psc0;
        writeBuf[2] = 0;
        write(I2CFile, writeBuf, 2);
        changes_made++;
    }
    
    if(arguments.psc1_inuse)
    {
        printf("Writing PSC1 value of 0x%02X\n", arguments.psc1);
        writeBuf[0] = 0b00000011;           //no increment
        writeBuf[1] = arguments.psc1;
        writeBuf[2] = 0;
        write(I2CFile, writeBuf, 2);
        changes_made++;
    }
    
    if(arguments.pwm0_inuse)
    {
        printf("Writing PWM0 value of 0x%02X\n", arguments.pwm0);
        writeBuf[0] = 0b00000010;
        writeBuf[1] = arguments.pwm0;
        writeBuf[2] = 0;
        write(I2CFile, writeBuf, 2);
        changes_made++;
    }
    
    if(arguments.pwm1_inuse)
    {
        printf("Writing PWM1 value of 0x%02X\n", arguments.pwm1);
        writeBuf[0] = 0b00000100;
        writeBuf[1] = arguments.pwm1;
        writeBuf[2] = 0;
        write(I2CFile, writeBuf, 2);
        changes_made++;
    }
    
    uint8_t new_ls0 = curr_regs.LS0;
    
    if(arguments.led0_str != NULL)
    {
        new_ls0 = generate_new_ls0(new_ls0, arguments.led0_str, 0);
        //printf("LED0: new ls0 = 0x%02X\n", new_ls0);
    }
    
    if(arguments.led1_str != NULL)
    {
        new_ls0 = generate_new_ls0(new_ls0, arguments.led1_str, 1);
        //printf("LED1: new ls0 = 0x%02X\n", new_ls0);
    }
    
    if(arguments.led2_str != NULL)
    {
        new_ls0 = generate_new_ls0(new_ls0, arguments.led2_str, 2);
        //printf("LED2: new ls0 = 0x%02X\n", new_ls0);
    }
    
    if(arguments.led3_str != NULL)
    {
        new_ls0 = generate_new_ls0(new_ls0, arguments.led3_str, 3);
        //printf("LED3: new ls0 = 0x%02X\n", new_ls0);
    }
    
    
    if(new_ls0 != curr_regs.LS0)
    {
        writeBuf[0] = 0b00000101;       //LED select
        writeBuf[1] = new_ls0;
        writeBuf[2] = 0;
        write(I2CFile, writeBuf, 2);
        changes_made++;
    }
    
    if(changes_made)
    {
        printf("New Register Status:\n");
        get_curr_regs();
        print_curr_regs();
    }
    else
        printf("Not updating any registers.\n");
    
    close(I2CFile);
    
    return 0;
    
}


