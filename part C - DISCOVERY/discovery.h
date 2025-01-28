/*
 * @file ping.h
 * @version 1.0
 * @author Roy Simanovich
 * @date 2024-02-13
 * @brief Header file for the ping program.
 * @attention This header file is used by the ping.c file.
 * @note The header file contains the constants and function prototypes used by the program.
*/

#ifndef _DISCOVERY_H
#define _DISCOVERY_H

/****************************************************************************************
 * 										CONSTANTS										*
 ****************************************************************************************/

/*
 * @brief Timeout value in milliseconds for the poll(2) function.
 * @note The poll(2) function will wait for this amount of time for the socket to become ready for reading.
 * @attention If the socket is not ready for reading after this amount of time, the function will return 0.
 * @note The default value is 2000 milliseconds (2 seconds).
*/
#define TIMEOUT 100

/*
 * @brief Size of the buffer used to store the ICMP packet (including the header).
 * @note The buffer size is set to 1024 bytes, which is more than enough for the ICMP packet.
 * @attention The buffer size should be at least the size of the ICMP header, which is 8 bytes.
*/
#define BUFFER_SIZE 1024

/*
 * @brief The time to sleep between sending ping requests in seconds.
 * @note Default value is 1 second.
*/
#define SLEEP_TIME 1

/*
 * @brief Maximum number of requests to send to the destination address.
 * @note The program will send this amount of requests to the destination address.
 * @attention The default value is 0.
 * @note If the value is set to 0, the program will send requests indefinitely.
*/
#define MAX_REQUESTS 0

/*
 * @brief Maximum number of retries for the ping request.
 * @note The program will try to send the ping request this amount of times before giving up.
 * @attention The default value is 3.
*/
#define MAX_RETRY 3


/****************************************************************************************
 * 										FUNCTIONS										*
 ****************************************************************************************/

/*
 * @brief Calculate the checksum of a given data block, according to RFC 1071 and RFC 1624.
 * @param data Pointer to the data block.
 * @param bytes Length of the data block in bytes.
 * @return The checksum of the data block in 16-bit format.
 * @note This checksum function is compatible with the IP and ICMP protocols.
 * @note RFC 1071: https://tools.ietf.org/html/rfc1071
 * @note RFC 1624: https://tools.ietf.org/html/rfc1624
 * @warning For TCP and UDP protocols, the checksum is calculated differently.
 * @attention You don't actually need to understand what the hell is going on here, just use it.
*/
unsigned short int calculate_checksum(void *data, unsigned int bytes);
int is_valid_option(const char* option);
char* get_network_address (char * address , int subnet_mask);
void print_byte_as_binary(unsigned char byte);
int get_addresses_per_subnet(int subnet_mask);
int check_if_digit(char* message,int size);
#endif // _DISCOVERY_H