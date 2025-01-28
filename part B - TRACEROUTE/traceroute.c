/*
 * @file traceroute.c
 * @version 2.0
 * @author Dor Cohen
 * @date 2025-01-15
 * @brief A simple implementation of the traceroute program using raw sockets.
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
#include "traceroute.h" // Header file for the program (calculate_checksum function and some constants)
char *final_address = NULL; // initializing the destination_address
int ttl =1; // initializing ttl to start from 1 , to get to the closest router to our endpoint.
/*
 * @brief Main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 on success, 1 on failure.
 * @note The program requires one command-line argument: the destination IP address.
*/
int main(int argc, char *argv[]) {

	// Check the number of command-line arguments.
	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s -a <destination_ip>\n", argv[0]);
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
            	final_address = argv[++i];
        	} 
			else {
            fprintf(stderr, "Error: -a flag requires an address\n");
            return 1;
			}
		}
	}

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


	// Reset the destination address structure to zero, to make sure there are no garbage values.
	// As we only need to set the IP address and the family, we can set the rest of the structure to zero.
	memset(&destination_address, 0, sizeof(destination_address));

	// We need to set the family of the destination address to AF_INET, as we are using the IPv4 protocol.
	destination_address.sin_family = AF_INET;

	// Try to convert the destination IP address from the user input to a binary format.
	// Could fail if the IP address is not valid.
	if (inet_pton(AF_INET, final_address, &destination_address.sin_addr) <= 0)
	{
		fprintf(stderr, "Error: \"%s\" is not a valid IPv4 address\n", final_address);
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

	fprintf(stdout, "traceroute to %s with %d hops max:\n", final_address, MAX_HOPS);


	// The main loop of the program.
	while (ttl <= 30) {
		if (setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
			perror("setsockopt(IP_TTL)");
			close(sock);
			return 1;
		}

		fprintf(stdout, "\n %d  ", ttl);
		int already_show = 0; // Ensure the IP is printed only once
		int no_response = 0;  // Track timeouts for the current TTL

		for (size_t i = 0; i < 3; i++) {
			memset(buffer, 0, sizeof(buffer)); // Clear buffer

			// Set up ICMP packet
			icmp_header.un.echo.sequence = htons(seq++);
			icmp_header.checksum = 0;
			memcpy(buffer, &icmp_header, sizeof(icmp_header));
			memcpy(buffer + sizeof(icmp_header), msg, payload_size);
			icmp_header.checksum = calculate_checksum(buffer, sizeof(icmp_header) + payload_size);
			((struct icmphdr *)buffer)->checksum = icmp_header.checksum;

			struct timeval start, end;
			gettimeofday(&start, NULL);

			if (sendto(sock, buffer, (sizeof(icmp_header) + payload_size), 0, 
					(struct sockaddr *)&destination_address, sizeof(destination_address)) <= 0) {
				perror("sendto(2)");
				close(sock);
				return 1;
			}

			int ret = poll(fds, 1, TIMEOUT);

			if (ret == 0) {
				// Timeout for this packet
				fprintf(stdout, "  *  ");
				no_response++;
				continue; // Try the next packet
			} else if (ret < 0) {
				perror("poll(2)");
				close(sock);
				return 1;
			}

			if (fds[0].revents & POLLIN) {
				struct sockaddr_in source_address;
				memset(buffer, 0, sizeof(buffer));
				memset(&source_address, 0, sizeof(source_address));

				if (recvfrom(sock, buffer, sizeof(buffer), 0, 
							(struct sockaddr *)&source_address, &(socklen_t){sizeof(source_address)}) <= 0) {
					perror("recvfrom(2)");
					close(sock);
					return 1;
				}

				struct iphdr *ip_header = (struct iphdr *)buffer;
				struct icmphdr *icmp_reply = (struct icmphdr *)(buffer + ip_header->ihl * 4);
				gettimeofday(&end, NULL);

				float rtt = ((float)(end.tv_usec - start.tv_usec) / 1000) + 
							((end.tv_sec - start.tv_sec) * 1000);

				// Handle responses
				if (!already_show) {
					fprintf(stdout, "%s", get_readable_ip(ip_header->saddr));
					already_show = 1; // Ensure the IP is printed only once
				}
				fprintf(stdout, "   %.3fms", rtt);

				// If destination reached, mark as complete but continue printing for remaining packets
				if (icmp_reply->type == ICMP_ECHOREPLY && 
					source_address.sin_addr.s_addr == destination_address.sin_addr.s_addr) {
					if (i == 2) { // After printing the last response, exit
						fprintf(stdout, "\n");
						close(sock);
						return 0;
					}
				}
			}
		}

		fprintf(stdout, "\n"); // Move to the next TTL row
		++ttl;
	}
	//we will reach here only if the destination is unreachable so:
	fprintf(stderr,"Destination Unreachable. \n");
	close(sock);
	return 1;
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
    const char* valid_options[] = {"-a"};
    int num_options = sizeof(valid_options) / sizeof(valid_options[0]);

    for (int i = 0; i < num_options; i++) {
        if (strcmp(option, valid_options[i]) == 0) {
            return 1; // Valid option
        }
    }
    return 0; // Invalid option
}

// Function to convert raw IP address to a readable string
const char *get_readable_ip(uint32_t raw_ip) {
    static char ip_str[INET_ADDRSTRLEN]; // Buffer to store the string representation of the IP address

    struct in_addr ip_addr;
    ip_addr.s_addr = raw_ip; // Assign the raw IP to the in_addr structure

    // Convert to human-readable format
    if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
        perror("inet_ntop");
        return "Invalid IP";
    }

    return ip_str;
}