#include <bcm2835.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define LED_GREEN RPI_V2_GPIO_P1_11
#define LED_BLUE RPI_V2_GPIO_P1_13
#define LED_RED RPI_V2_GPIO_P1_15
#define SENS_ECHO_PIN RPI_V2_GPIO_P1_03
#define SENS_TRIG_PIN RPI_V2_GPIO_P1_05
#define SENS_PULSE 10
#define SENS_TIMEOUT 7000000
#define SENS_MAXDIST 30
#define SERVO_PIN RPI_V2_GPIO_P1_12
#define SERVO_PWM 20000
#define SERVO_OPEN_PER 2000
#define SERVO_CLOSE_PER 1000
#define SERVO_MOVE_LOOP 20

#define PORT 8080
#define GATE_CLOSED 0
#define GATE_OPEN 1

// Globals
uint8_t gate_status = GATE_CLOSED;
uint32_t sens_distance = 0;
uint8_t sens_presence = 0;
uint8_t gate_open(void);
uint8_t gate_close(void);
uint8_t read_distance(void);

// Main
int main(int argc, char **argv)
{

    int socket_desc, new_socket, c;
    struct sockaddr_in server, client;
    char buffer[1024];
    char response[1024];
    char response_header[1024];

    // BCM
    if (!bcm2835_init())
    {
        printf("bcm2835_init failed. Are you running as root?\n");
        return 1;
    }

    bcm2835_gpio_fsel(LED_GREEN, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(LED_BLUE, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(LED_RED, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(SERVO_PIN, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(SENS_ECHO_PIN, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(SENS_TRIG_PIN, BCM2835_GPIO_FSEL_OUTP);
    gate_close();
    read_distance();

    // Server
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        printf("Could not create socket!\n");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        printf("Bind failed!\n");
        return 1;
    }

    listen(socket_desc, 3);

    printf("Service ONLINE\nWaiting for incoming connections...\n");
    c = sizeof(struct sockaddr_in);
    while ((new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c)))
    {
        memset(buffer, 0, sizeof(buffer));
        recv(new_socket, buffer, sizeof(buffer), 0);
        char *password_start = strstr(buffer, "Authorization: Basic ");

        if (password_start == NULL || strncmp(password_start + 21, "OnRlc3Q=", 8) != 0)
        { // ENCODED "test" : OnRlc3Q=
            sprintf(response_header, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"Access to Gate Control Test\"\r\n\r\n");
            send(new_socket, response_header, strlen(response_header), 0);
        }
        else
        {
            if (strstr(buffer, "GET /?gate=open"))
            {
                gate_open();
            }
            else if (strstr(buffer, "GET /?gate=close"))
            {
                gate_close();
            }
            else if (strstr(buffer, "GET /?gate=read"))
            {
                read_distance();
                if (sens_presence)
                {
                    gate_open();
                    while (sens_presence)
                    {
                        read_distance();
                        bcm2835_delay(2000);
                    }
                    bcm2835_delay(4000);
                    gate_close();
                }
                else
                    gate_close();
            }
            // set the response content
            sprintf(response, "<html><body><h1>Gate Control Test</h1><p>Gate is currently %s.</p><p>Last read distance: %d cm.</p><form action='/' method='get'><input type='submit' name='gate' value='open'> <input type='submit' name='gate' value='close'> <input type='submit' name='gate' value='read'></form></body></html>", (gate_status) ? "OPEN" : "CLOSED", (sens_distance));

            // set the response header
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", (int)strlen(response));

            // send the response header and content
            send(new_socket, response_header, strlen(response_header), 0);
            send(new_socket, response, strlen(response), 0);
        }
        close(new_socket);
    }
    return 0;
}

// Func
uint8_t read_distance(void)
{
    uint32_t i = 0;
    uint64_t start_time;
    uint64_t currRead = 0;
    printf("Reading Distance...\n");

    bcm2835_gpio_write(SENS_TRIG_PIN, LOW);
    bcm2835_delayMicroseconds(SENS_PULSE);
    bcm2835_gpio_write(SENS_TRIG_PIN, HIGH);
    bcm2835_delayMicroseconds(SENS_PULSE);
    bcm2835_gpio_write(SENS_TRIG_PIN, LOW);

    for (i = 0; (i < SENS_TIMEOUT) && (bcm2835_gpio_lev(SENS_ECHO_PIN) == LOW); i++)
    {
        // printf("Step 1: %i\n", i);
    }
    start_time = bcm2835_st_read();

    for (i = 0; (i < SENS_TIMEOUT) && (bcm2835_gpio_lev(SENS_ECHO_PIN) == HIGH); i++)
    {
        // printf("Step 1: %i\n", i);
    }

    currRead = ((bcm2835_st_read() - start_time) * 17150 / 1000000);

    if (currRead == 0)
    {
        printf("Read Failed.\n");
        return 1;
    }
    else
    {
        sens_distance = currRead;
        if (sens_distance < SENS_MAXDIST)
        {
            bcm2835_gpio_write(LED_BLUE, HIGH);
            sens_presence = 1;
        }
        else
        {
            bcm2835_gpio_write(LED_BLUE, LOW);
            sens_presence = 0;
        }
        printf("Distance: %d.\n", sens_distance);
        return 0;
    }
    return 1;
}

uint8_t gate_open(void)
{
    uint8_t i = 0;
    printf("Opening Gate...\n");

    for (i = 0; i < SERVO_MOVE_LOOP; i++)
    {
        bcm2835_gpio_write(SERVO_PIN, HIGH);
        bcm2835_delayMicroseconds(SERVO_OPEN_PER);
        bcm2835_gpio_write(SERVO_PIN, LOW);
        bcm2835_delayMicroseconds(SERVO_PWM);
    }
    bcm2835_gpio_write(LED_GREEN, HIGH);
    bcm2835_gpio_write(LED_RED, LOW);
    gate_status = GATE_OPEN;
    printf("Gate OPEN.\n");
    return 0;
}

uint8_t gate_close(void)
{
    uint8_t i = 0;
    printf("Closing Gate...\n");

    for (i = 0; i < SERVO_MOVE_LOOP; i++)
    {
        bcm2835_gpio_write(SERVO_PIN, HIGH);
        bcm2835_delayMicroseconds(SERVO_CLOSE_PER);
        bcm2835_gpio_write(SERVO_PIN, LOW);
        bcm2835_delayMicroseconds(SERVO_PWM);
    }
    bcm2835_gpio_write(LED_GREEN, LOW);
    bcm2835_gpio_write(LED_RED, HIGH);
    gate_status = GATE_CLOSED;
    printf("Gate CLOSED.\n");
    return 0;
}
