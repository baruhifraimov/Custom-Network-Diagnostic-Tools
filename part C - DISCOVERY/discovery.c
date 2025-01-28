/*
 * @file discovery.c
 * @version 2.0
 * @author Dor Cohen
 * @date 2025-01-16
 * @brief A simple implementation of the ping program using raw sockets.
 * @note The program sends an ICMP ECHO REQUEST packet to the destination address and waits for an ICMP ECHO REPLY packet.
*/

#include <stdio.h> // Standard input/output definitions
#include <arpa/inet.h> // Definitions for internet operations (inet_pton, inet_ntoa)
#include <netinet/in.h> // Internet address family (AF_INET, AF_INET6)
#include <netinet/ip.h> // Definitions for internet protocol operations (IP header)
#include <netinet/ip_icmp.h> // Definitions for internet control message protocol operations (ICMP header)
#include <poll.h> // Poll API for monitoring file descriptors (poll)
#include <errno.h> // Error number definitions. Used for error handling (EACCES, EPERM)
#include <string.h> // String manipulation functions (strlen, memset, memcpy)
#include <sys/socket.h> // Definitions for socket operations (socket, sendto, recvfrom)
#include <sys/time.h> // Time types (struct timeval and gettimeofday)
#include <unistd.h> // UNIX standard function definitions (getpid, close, sleep)
#include "discovery.h" // Header file for the program (calculate_checksum function and some constants)
#include <stdlib.h>
#include <arpa/inet.h>
#include <math.h>
#include <ctype.h>

char *address = NULL; // initializing the destination_address
int subnet_mask = -1;
/*
 * @brief Main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 on success, 1 on failure.
 * @note The program requires one command-line argument: the destination IP address.
*/
int main(int argc, char *argv[]) {

	// Check the number of command-line arguments.
	if (argc != 5)
	{
		fprintf(stderr, "Usage: %s -a <destination_ip> -c <subnet_mask>\n", argv[0]);
		return 1;
	}
	for (int i = 1; i < argc; i++)
	{
		if (!is_valid_option(argv[i])){
			fprintf(stderr, "Error: '%s' is an invalid arguments\n",argv[i]);
			return 1;
		}
		if (strcmp(argv[i], "-a") == 0) {
        	if (i + 1 < argc) {
            	address = argv[++i];
        	} 
			else {
            fprintf(stderr, "Error: -a flag requires an address\n");
            return 1;
			}
		}
		else if (strcmp(argv[i], "-c") == 0)
		{
			if (i+1 < argc && !check_if_digit(argv[i+1],strlen(argv[i+1]))){
					subnet_mask = atoi(argv[++i]);
					if (subnet_mask < 0 || subnet_mask > 32 ){
						fprintf(stderr, "Error: -C flag requires a num between 0 to 32 to determine the subnet mask. \n");
						return 1;
					}
			}
			else {
				fprintf(stderr, "Error: -c flag requires a number between 0 to 32 after it.\n");
				return 1;
			}
		}
	}

	char *network_address = get_network_address(address,subnet_mask);
	int num_of_addresses = get_addresses_per_subnet(subnet_mask);

	// Structure to store the destination address.
	// Even though we are using raw sockets, creating from zero the IP header is a bit complex,
	// we use the structure to store the destination address.
	struct sockaddr_in destination_address;

	// Just some buffer to store the ICMP packet itself. We zero it out to make sure there are no garbage values.
	char buffer[BUFFER_SIZE] = {0};

	// The payload of the ICMP packet. Can be anything, as long as it's a valid string.
	// We use some garbage characters, as well as some ASCII characters, to test the program.
	char *msg = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()_+{}|:<>?~`-=[]',.";

	// Payload size of the ICMP packet.
	// We need to add 1 to the size of the payload, as we need to include the null-terminator of the string.
	int payload_size = strlen(msg) + 1;

	// Number of retries for the ping request.
	int retries = 0;

	// Reset the destination address structure to zero, to make sure there are no garbage values.
	// As we only need to set the IP address and the family, we can set the rest of the structure to zero.
	memset(&destination_address, 0, sizeof(destination_address));

	// We need to set the family of the destination address to AF_INET, as we are using the IPv4 protocol.
	destination_address.sin_family = AF_INET;

	// Try to convert the destination IP address from the user input to a binary format.
	// Could fail if the IP address is not valid.
	if (inet_pton(AF_INET, network_address, &destination_address.sin_addr) <= 0)
	{
		fprintf(stderr, "Error: \"%s\" is not a valid IPv4 address\n", address);
		return 1;
	}

	// Create a raw socket with the ICMP protocol.
	int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

	// Error handling if the socket creation fails (could happen if the program isn't run with sudo).
	if (sock < 0)
	{
		perror("socket(2)");

		// Check if the error is due to permissions and print a message to the user.
		// Some magic constants for the error numbers, which are defined in the errno.h header file.
		if (errno == EACCES || errno == EPERM)
			fprintf(stderr, "You need to run the program with sudo.\n");
		
		return 1;
	}

	// Create an ICMP header structure and set the fields to the desired values.
	struct icmphdr icmp_header;

	// Set the type of the ICMP packet to ECHO REQUEST (PING).
	icmp_header.type = ICMP_ECHO;

	// Set the code of the ICMP packet to 0 (As it isn't used in the ECHO type).
	icmp_header.code = 0;

	// Set the ping identifier to the process ID of the program.
	// This field is used to identify the ping request.
	// Each program has a different process ID, so it's a good value to use.
	icmp_header.un.echo.id = htons(getpid());

	// The sequence number of the ping request.
	// It starts at 0 and is incremented by 1 for each new request.
	// Good for identifying the order of the requests.
	int seq = 0;

	// Create a pollfd structure to wait for the socket to become ready for reading.
	// Used for receiving the ICMP reply packet, as it may take some time to arrive or not arrive at all.
	struct pollfd fds[1];

	// Set the file descriptor of the socket to the pollfd structure.
	fds[0].fd = sock;

	// Set the events to wait for to POLLIN, which means the socket is ready for reading.
	fds[0].events = POLLIN;

	int iterator=0;
	fprintf(stdout, "scanning %s /%d for %d addresses \n", network_address, subnet_mask,num_of_addresses);
	// The main loop of the program.
	while (iterator < num_of_addresses) {
		// Print debugging information (optional)
		//printf("Checking IP: %s\n", inet_ntoa(destination_address.sin_addr));
		// Zero out the buffer to ensure no garbage data
		memset(buffer, 0, sizeof(buffer));

		// Set up the ICMP packet
		icmp_header.un.echo.sequence = htons(seq++); // Sequence number
		icmp_header.checksum = 0;                   // Reset checksum
		memcpy(buffer, &icmp_header, sizeof(icmp_header));
		memcpy(buffer + sizeof(icmp_header), msg, payload_size);
		icmp_header.checksum = calculate_checksum(buffer, sizeof(icmp_header) + payload_size);
		((struct icmphdr *)buffer)->checksum = icmp_header.checksum;

		// Send the ICMP packet to the current address
		if (sendto(sock, buffer, (sizeof(icmp_header) + payload_size), 0, (struct sockaddr *)&destination_address, sizeof(destination_address)) <= 0) {
			perror("sendto failed");
			retries = 0;      // Reset retries
			iterator++;       // Move to the next address
			destination_address.sin_addr.s_addr = htonl(ntohl(destination_address.sin_addr.s_addr) + 1); // Increment address
			continue;
		}

		// Poll for a reply
		int ret = poll(fds, 1, TIMEOUT);
		if (ret == 0) {
			// Timeout occurred
			if (++retries >= MAX_RETRY) {
				retries = 0;      // Reset retries
				iterator++;       // Move to the next address
				destination_address.sin_addr.s_addr = htonl(ntohl(destination_address.sin_addr.s_addr) + 1); // Increment address
			}
			continue;
		} else if (ret < 0) {
			// Poll error
			perror("poll failed");
			retries = 0;      // Reset retries
			iterator++;       // Move to the next address
			destination_address.sin_addr.s_addr = htonl(ntohl(destination_address.sin_addr.s_addr) + 1); // Increment address
			continue;
		}

		// If data is available to read
		if (fds[0].revents & POLLIN) {
			struct sockaddr_in source_address;
			memset(&source_address, 0, sizeof(source_address));

			// Receive the ICMP reply
			if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&source_address, &(socklen_t){sizeof(source_address)}) <= 0) {
				perror("recvfrom failed");
			} else {
				struct iphdr *ip_header = (struct iphdr *)buffer;
				struct icmphdr *icmp_reply = (struct icmphdr *)(buffer + ip_header->ihl * 4);

				// If it's an ECHO REPLY, print the result in green (once per successful reply)
				if (icmp_reply->type == ICMP_ECHOREPLY) {
					printf("\033[0;32m%s\033[0m\n", inet_ntoa(destination_address.sin_addr)); // Print in green
					retries = 0; // Reset retries after success
					iterator++;  // Move to the next address
					destination_address.sin_addr.s_addr = htonl(ntohl(destination_address.sin_addr.s_addr) + 1); // Increment address
					continue;
				}
			}
		}

		// If we reached here, we need to move to the next address (e.g., retries exceeded or no reply)
		retries = 0;      // Reset retries
		iterator++;       // Move to the next address
		destination_address.sin_addr.s_addr = htonl(ntohl(destination_address.sin_addr.s_addr) + 1); // Increment address
	}

	// Print "Scan Complete!" at the end
	printf("Scan Complete!\n");
}

unsigned short int calculate_checksum(void *data, unsigned int bytes) {
	unsigned short int *data_pointer = (unsigned short int *)data;
	unsigned int total_sum = 0;

	// Main summing loop.
	while (bytes > 1)
	{
		total_sum += *data_pointer++; // Some magic pointer arithmetic.
		bytes -= 2;
	}

	// Add left-over byte, if any.
	if (bytes > 0)
		total_sum += *((unsigned char *)data_pointer);

	// Fold 32-bit sum to 16 bits.
	while (total_sum >> 16)
		total_sum = (total_sum & 0xFFFF) + (total_sum >> 16);

	// Return the one's complement of the result.
	return (~((unsigned short int)total_sum));
}

int is_valid_option(const char* option) {
    const char* valid_options[] = {"-a","-c"};
    int num_options = sizeof(valid_options) / sizeof(valid_options[0]);

    for (int i = 0; i < num_options; i++) {
        if (strcmp(option, valid_options[i]) == 0) {
            return 1; // Valid option
        }
    }
    return 0; // Invalid option
}

char* get_network_address (char * address , int subnet_mask) {
	struct in_addr addr, network_addr, binary_mask;
    
    char *result = malloc(INET_ADDRSTRLEN);
    if (result == NULL) {  // Check if memory allocation fails
        perror("Failed to allocate memory");
        return NULL;
    }

	// Convert the IP address to binary
	if (inet_pton(AF_INET, address, &addr) == 0) {
		fprintf(stderr, "Error: \"%s\" is not a valid IPv4 address\n", address);
		free(result);  // Free allocated memory on error
		return NULL;
	} else if (inet_pton(AF_INET, address, &addr) < 0) {
		perror("inet_pton failed");  // This handles unexpected failures
		free(result);  // Free allocated memory on error
		return NULL;
	}
	
	// Convert the subnet mask to binary
    uint32_t mask = (0xFFFFFFFF << (32 - subnet_mask)) & 0xFFFFFFFF;
    binary_mask.s_addr = htonl(mask);  // Convert to network byte order

    // Calculate the network address (IP & Subnet Mask)
    network_addr.s_addr = addr.s_addr & binary_mask.s_addr;
    
   // Convert the network address back to string format
    if (inet_ntop(AF_INET, &network_addr, result, INET_ADDRSTRLEN) == NULL) {
        perror("Failed to convert network address to string");
        free(result);  // Fix 2: Free allocated memory on error
        return NULL;
    }

	return result;
}

int get_addresses_per_subnet(int subnet_mask){
	return pow(2,(32-subnet_mask))-1;
}

int check_if_digit(char* message,int size){
    int count =0;
    for (int i = 0; i < size; i++)
    {
        if(isdigit(message[i])){
            // have a digit
            count++;
        }
    }
    // doesnt have a digit
    return size-count;
}
