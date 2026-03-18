/*
 * ESP8266_HAL.c
 *
 *  Created on: Apr 14, 2020
 *      Author: Controllerstech
 */


#include "UartRingbuffer_multi.h"
#include "ESP8266_HAL.h"
#include "stdio.h"
#include "string.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define wifi_uart &huart1
#define pc_uart &huart2


char buffer[20];
int setpoint_temp = 20; // Temperatura predefinita
int impianto_attivo = 0; // 0 = Spento, 1 = Acceso


char *Basic_inclusion = "<!DOCTYPE html><html>\n<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
		"<title>TERMOSTATO SMART</title>\n<style>"
		"html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n"
		"body{margin-top: 30px;} h1 {color: #444;}\n"
		".status-dot {height: 50px; width: 50px; border-radius: 50%; display: inline-block; margin: 20px;}\n"
		".red {background-color: red;} .green {background-color: green;}\n"
		".btn {display: inline-block; width: 60px; background-color: #34495e; color: white; padding: 15px; text-decoration: none; font-size: 20px; margin: 5px; border-radius: 4px;}\n"
		".btn-pwr {width: 140px; background-color: #1abc9c;}\n"
		".off {background-color: #7f8c8d;}\n"
		"</style></head>\n<body>\n<h1>TERMOSTATO</h1>\n";

// Stringhe dinamiche che verranno composte nella Server_Handle
char *Termo_ON = "<h3>STATO: ACCESO</h3><div class=\"status-dot green\"></div><br>"
				 "<a class=\"btn off\" href=\"/off\">SPEGNI IMPIANTO</a>";
char *Termo_OFF = "<h3>STATO: SPENTO</h3><div class=\"status-dot red\"></div><br>"
				  "<a class=\"btn btn-pwr\" href=\"/on\">ACCENDI IMPIANTO</a>";

char *Temp_Controls = "<hr><h3>SETPOINT TEMPERATURA</h3>"
					  "<a class=\"btn\" href=\"/dec\">-</a>"
					  "<span style=\"font-size: 30px; font-weight: bold; margin: 0 15px;\"> %d °C </span>"
					  "<a class=\"btn\" href=\"/inc\">+</a>";

char *Terminate = "</body></html>";

/*****************************************************************************************************************************************/

void ESP_Init (char *SSID, char *PASSWD)
{
	char data[80];

	Ringbuf_init();

	Uart_sendstring("AT+RST\r\n", wifi_uart);
	Uart_sendstring("RESETTING.", pc_uart);
	for (int i=0; i<5; i++)
	{
		Uart_sendstring(".", pc_uart);
		HAL_Delay(1000);
	}

	/********* AT **********/
	Uart_sendstring("AT\r\n", wifi_uart);
	while(!(Wait_for("AT\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("AT---->OK\n\n", pc_uart);


	/********* AT+CWMODE=1 **********/
	Uart_sendstring("AT+CWMODE=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CWMODE=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CW MODE---->1\n\n", pc_uart);


	/********* AT+CWJAP="SSID","PASSWD" **********/
	Uart_sendstring("connecting... to the provided AP\n", pc_uart);
	sprintf (data, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASSWD);
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for("WIFI GOT IP\r\n\r\nOK\r\n", wifi_uart)));
	sprintf (data, "Connected to,\"%s\"\n\n", SSID);
	Uart_sendstring(data,pc_uart);


	/********* AT+CIFSR **********/
	Uart_sendstring("AT+CIFSR\r\n", wifi_uart);
	while (!(Wait_for("CIFSR:STAIP,\"", wifi_uart)));
	while (!(Copy_upto("\"",buffer, wifi_uart)));
	while (!(Wait_for("OK\r\n", wifi_uart)));
	int len = strlen (buffer);
	buffer[len-1] = '\0';
	sprintf (data, "IP ADDR: %s\n\n", buffer);
	Uart_sendstring(data, pc_uart);


	Uart_sendstring("AT+CIPMUX=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CIPMUX=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CIPMUX---->OK\n\n", pc_uart);

	Uart_sendstring("AT+CIPSERVER=1,80\r\n", wifi_uart);
	while (!(Wait_for("OK\r\n", wifi_uart)));
	Uart_sendstring("CIPSERVER---->OK\n\n", pc_uart);

	Uart_sendstring("Now Connect to the IP ADRESS\n\n", pc_uart);

}




int Server_Send (char *str, int Link_ID)
{
    int len = strlen (str);
    char data[80];

    // Comunichiamo all'ESP quanti byte stiamo per inviare
    sprintf (data, "AT+CIPSEND=%d,%d\r\n", Link_ID, len);
    Uart_sendstring(data, wifi_uart);

    // Attendiamo il prompt '>'
    while (!(Wait_for(">", wifi_uart)));

    // Inviamo la stringa HTML
    Uart_sendstring (str, wifi_uart);

    // Attendiamo la conferma dell'invio
    while (!(Wait_for("SEND OK", wifi_uart)));

    // Chiudiamo SOLO il Link_ID corrente per liberare la connessione al browser
    sprintf (data, "AT+CIPCLOSE=%d\r\n", Link_ID);
    Uart_sendstring(data, wifi_uart);

    while (!(Wait_for("OK\r\n", wifi_uart)));

    return 1;
}

void Server_Handle (char *str, int Link_ID)
{
    // Aumentiamo leggermente il buffer se necessario,
    // ma assicuriamoci che sia pulito (azzerato)
    char datatosend[1024] = {0};
    char temp_buf[256] = {0};

    // FONDAMENTALE: Intestazione HTTP minima
    // Senza questa, molti browser mostrano una pagina bianca
    strcpy(datatosend, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");

    // Parte HTML Iniziale
    strcat(datatosend, Basic_inclusion);

    // Stato impianto e spia
    if (impianto_attivo) {
        strcat(datatosend, Termo_ON);
    } else {
        strcat(datatosend, Termo_OFF);
    }

    // Controlli temperatura
    sprintf(temp_buf, Temp_Controls, setpoint_temp);
    strcat(datatosend, temp_buf);

    // Chiusura HTML
    strcat(datatosend, Terminate);

    // Invio effettivo
    Server_Send(datatosend, Link_ID);
}

void Server_Start (void)
{
	char buftocopyinto[64] = {0};
	char Link_ID;
	while (!(Get_after("+IPD,", 1, &Link_ID, wifi_uart)));
	Link_ID -= 48;
	while (!(Copy_upto(" HTTP/1.1", buftocopyinto, wifi_uart)));

	if (Look_for("/on", buftocopyinto) == 1) {
		impianto_attivo = 1;
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1); // Accendi LED fisico
	}
	else if (Look_for("/off", buftocopyinto) == 1) {
		impianto_attivo = 0;
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0); // Spegni LED fisico
	}
	else if (Look_for("/inc", buftocopyinto) == 1) {
		if(setpoint_temp < 30) setpoint_temp++; // Limite max 30 gradi
	}
	else if (Look_for("/dec", buftocopyinto) == 1) {
		if(setpoint_temp > 5) setpoint_temp--;  // Limite min 5 gradi
	}

	// Gestione favicon per evitare doppi invii
	if (Look_for("/favicon.ico", buftocopyinto) == 1) return;

	// In ogni caso, rinfresca la pagina con i nuovi dati
	Server_Handle(" ", Link_ID);
}
