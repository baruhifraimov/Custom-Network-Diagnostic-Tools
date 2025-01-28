/*
 * @file ping.c
 * @version 1.0
 * @author Roy Simanovich
 * @date 2024-02-13
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
#include "ping.h" // Header file for the program (calculate_checksum function and some constants)
#include <stdlib.h>
#include <ctype.h>
#include <netinet/in.h>
#include <signal.h>
#include <math.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
/*
 * @brief Main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 on success, 1 on failure.
 * @note The program requires one command-line argument: the destination IP address.
*/
char *address = NULL; // initializing the destination_address
int type = 0;// initializing the type of the ip to ping , ipv4 or ipv6
int count = -1;// initializing the count of the packets that will be sent
int flood = 0;//initializing flood to be default zero , if parameter -f is being forward than it will be 1
int sock = 0; //initializing the socket to 0.
// The sequence number of the ping request.
// It starts at 0 and is incremented by 1 for each new request.
// Good for identifying the order of the requests.
volatile int seq = 0;

// initializing the packet that has being received by 0 , each time a packet will be received will increase by 1.
volatile int packets_received = 0;

// initializing the total time of the whole packets 
float total_time = 0;

//initializing the rtt definers:
float rtt_min = -1.0;       // Smallest RTT observed (-1 means no packets received yet)
float rtt_max = -1.0;       // Largest RTT observed
float rtt_sum = 0.0;        // Sum of all RTTs
float rtt_sum_squares = 0.0; // Sum of squared RTTs


int main(int argc, char *argv[]) {

	// Check the number of command-line arguments. we must include -a <address> , -t <type> so min 5 arguments
	//argv[0] - ping - the program name
	//argv[1] = -a
	//argv[2] - address the address to ping
	//argv[3] = -t
	//argv[4] = type - type of the communication ipv4/6
	//argv[5] - -c
	//argv[6] - count - counted number of pings
	//argv[7] - -f - flood - if included will send them back to back
	int infinite = 0;
	if (argc < 5 || argc > 8)
	{
		fprintf(stderr, "Usage: %s -a <address> -t <4|6> [-c <count>] [-f]\n", argv[0]);
		return 1;
	}

	for (int i = 1; i < argc; i++)
	{
		if (!is_valid_option(argv[i])){
			fprintf(stderr, "Error: There are invalid arguments\n");
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
		else if (strcmp(argv[i], "-t") == 0)
		{
			if (i+1 < argc){
				type = atoi(argv[++i]);
				if (type != 6 && type != 4){
					fprintf(stderr, "Error: -t flag requires a num 4 or 6 to determine ip type. \n");
					return 1;
				}
			}
			else {
				fprintf(stderr, "Error: -t flag requires an address\n");
				return 1;
			}
		}

		else if (strcmp(argv[i], "-c") == 0)
		{
			if (i + 1 < argc) {
				char *param = argv[++i];
				int is_number = 1;

				// Check if the parameter is a number
				for (int j = 0; param[j] != '\0'; j++) {
					if (!isdigit((unsigned char)param[j])) {
						is_number = 0;
						break;
					}
				}

				if (is_number) {
					count = atoi(param); // Convert to integer
					if (count <= 0) { // Check if it's greater than 0
						fprintf(stderr, "Error: -c flag requires a number greater than 0.\n");
						return 1;
					}
				}
				else {
					fprintf(stderr, "Error: -c flag requires a numeric value.\n");
					return 1;
				}
			} 
			else {
				fprintf(stderr, "Error: -c flag requires a parameter.\n");
				return 1;
			}
		}
	
		else if (strcmp(argv[i], "-f") == 0)
		{
			// Ensure the flood flag is set only once for clarity
			if (flood) {
				fprintf(stderr, "Error: Duplicate -f flag detected.\n");
				return 1;
			}

			// Enable flood mode
			flood = 1;

		}
	}


	// Structure to store the destination address.
	// Even though we are using raw sockets, creating from zero the IP header is a bit complex,
	// we use the structure to store the destination address.

    	struct sockaddr_in destination_address_v4;
    	struct sockaddr_in6 destination_address_v6;
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
	memset(&destination_address_v4, 0, sizeof(destination_address_v4));
	memset(&destination_address_v6, 0, sizeof(destination_address_v6));


	// We need to set the family of the destination address to AF_INET, as we are using the IPv4 protocol.
	destination_address_v4.sin_family = AF_INET;
	destination_address_v6.sin6_family = AF_INET6;
	// Try to convert the destination IP address from the user input to a binary format.
	// Could fail if the IP address is not valid.
	if (type == 4)
	{
		if (inet_pton(AF_INET, address, &destination_address_v4.sin_addr) <= 0 )
		{
			fprintf(stderr, "Error: \"%s\" is not a valid IPv4 address\n", address);
			return 1;
		}
	}
	else
	{
		if (inet_pton(AF_INET6, address, &destination_address_v6.sin6_addr) <= 0){
			fprintf(stderr, "Error: \"%s\" is not a valid  IPv6 address\n", address);
			return 1;
		}
	}
	

	// Create a raw socket with the ICMP protocol.
	if (type ==4){
		sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	}
	else{
		sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	}
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
	struct icmp6_hdr icmp6_header;
	
	// Set the type of the ICMP packet to ECHO REQUEST (PING).
	icmp_header.type = ICMP_ECHO;
	icmp6_header.icmp6_type = ICMP6_ECHO_REQUEST;
	// Set the code of the ICMP packet to 0 (As it isn't used in the ECHO type).
	icmp_header.code = 0;
	icmp6_header.icmp6_code = 0;
	// Set the ping identifier to the process ID of the program.
	// This field is used to identify the ping request.
	// Each program has a different process ID, so it's a good value to use.
	icmp_header.un.echo.id = htons(getpid());
	icmp6_header.icmp6_id = htons(getpid());


	// Create a pollfd structure to wait for the socket to become ready for reading.
	// Used for receiving the ICMP reply packet, as it may take some time to arrive or not arrive at all.
	struct pollfd fds[1];

	// Set the file descriptor of the socket to the pollfd structure.
	fds[0].fd = sock;

	// Set the events to wait for to POLLIN, which means the socket is ready for reading.
	fds[0].events = POLLIN;
	
	signal(SIGINT, display_statistics);

	fprintf(stdout, "PING %s with %d bytes of data:\n", address, payload_size);

	// The main loop of the program.
	if (count==-1){
		infinite = 1;
	}
	while (count>0 || infinite==1)
	{
		// Zero out the buffer to make sure there are no garbage values.
		memset(buffer, 0, sizeof(buffer));

		if (type == 4) {
			// Set the sequence number of the ping request, and update the value for the next request.
			icmp_header.un.echo.sequence = htons(seq++);

			// Set the checksum of the ICMP packet to 0, as we need to calculate it.
			icmp_header.checksum = 0;

			// Copy the ICMP header structure to the buffer.
			memcpy(buffer, &icmp_header, sizeof(icmp_header));

			// Copy the payload of the ICMP packet to the buffer.
			memcpy(buffer + sizeof(icmp_header), msg, payload_size);

			// Calculate the checksum of the ICMP packet.
			icmp_header.checksum = calculate_checksum(buffer, sizeof(icmp_header) + payload_size);

			// Set the checksum of the ICMP packet to the calculated value.
			struct icmphdr *pckt_hdr = (struct icmphdr *)buffer;
			pckt_hdr->checksum = icmp_header.checksum;

			// Calculate the time it takes to send and receive the packet.
			struct timeval start, end;
			gettimeofday(&start, NULL);

			// Try to send the ICMP packet to the destination address.
			if (sendto(sock, buffer, (sizeof(icmp_header) + payload_size), 0, 
					(struct sockaddr *)&destination_address_v4, sizeof(destination_address_v4)) <= 0) {
				perror("ipv4 sendto(2)");
				close(sock);
				return 1;
			}

			// Poll the socket to wait for the ICMP reply packet.
			int ret = poll(fds, 1, TIMEOUT);

			// Check for timeout or errors.
			if (ret == 0) {
				if (++retries == MAX_RETRY) {
					fprintf(stderr, "Request timeout for icmp_seq %d, aborting.\n", seq);
					break;
				}

				fprintf(stderr, "Request timeout for icmp_seq %d, retrying...\n", seq);
				--seq; // Decrement the sequence number to send the same request again.
				continue;
			} else if (ret < 0) {
				perror("poll(2)");
				close(sock);
				return 1;
			}

			// Check if the socket has data to read.
			if (fds[0].revents & POLLIN) {
				struct sockaddr_in source_address_v4;

				memset(buffer, 0, sizeof(buffer));
				memset(&source_address_v4, 0, sizeof(source_address_v4));

				ssize_t bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0, 
												(struct sockaddr *)&source_address_v4, 
												&(socklen_t){sizeof(source_address_v4)});
				if (bytes_received > 0) {
					packets_received++; // Increment received packets counter
				} else {
					perror("recvfrom(2)");
					close(sock);
					return 1;
				}

				retries = 0;
				gettimeofday(&end, NULL);

				struct iphdr *ip_header = (struct iphdr *)buffer;
				struct icmphdr *icmp_header = (struct icmphdr *)(buffer + ip_header->ihl * 4);

				if (icmp_header->type == ICMP_ECHOREPLY) {
					float pingPongTime = ((float)(end.tv_usec - start.tv_usec) / 1000) + 
										((end.tv_sec - start.tv_sec) * 1000);
					total_time += pingPongTime;
					update_rtt_stats(pingPongTime);

					fprintf(stdout, "%ld bytes from %s: icmp_seq=%d ttl=%d time=%.2fms\n",
							(ntohs(ip_header->tot_len) - (ip_header->ihl * 4) - sizeof(struct icmphdr)), 
							inet_ntoa(source_address_v4.sin_addr),
							ntohs(icmp_header->un.echo.sequence),
							ip_header->ttl, 
							pingPongTime);

					if (seq == MAX_REQUESTS) {
						break;
					}
				} else {
					fprintf(stderr, "Error: packet received with type %d\n", icmp_header->type);
				}
			}

			count--;

			if (flood == 0) {
				sleep(SLEEP_TIME);
			}
		}
		else{

			// Set the sequence number of the ping request, and update the value for the next request.
			icmp6_header.icmp6_seq = htons(seq++);
			
			// Set the checksum of the ICMP packet to 0, as we need to calculate it.
			icmp6_header.icmp6_cksum=0;
			
			// Copy the ICMP header structure to the buffer.
			memcpy(buffer, &icmp6_header, sizeof(icmp6_header));


			// Copy the payload of the ICMP packet to the buffer.
			memcpy(buffer + sizeof(icmp6_header), msg, payload_size);

			// Calculate the checksum of the ICMP packet.
			struct sockaddr_in6 local_address;
			socklen_t addr_len = sizeof(local_address);
			if (getsockname(sock, (struct sockaddr *)&local_address, &addr_len) == -1) {
    			perror("getsockname");
    			close(sock);
    			return 1;
			}

			// Calculate the checksum of the ICMPv6 packet with the pseudo-header
			icmp6_header.icmp6_cksum = calculate_checksum_icmp6(buffer,
                                                    sizeof(icmp6_header) + payload_size,
                                                    &local_address.sin6_addr,
                                                    &destination_address_v6.sin6_addr,
                                                    IPPROTO_ICMPV6);

			// Set the checksum of the ICMP packet to the calculated value.
			// Instead of using the memcpy function, we can just set the value directly.
			struct icmp6_hdr *pckt6_hdr = (struct icmp6_hdr *)buffer;
			pckt6_hdr->icmp6_cksum= icmp6_header.icmp6_cksum;

			// Calculate the time it takes to send and receive the packet.
			struct timeval start, end;
			gettimeofday(&start, NULL);

			// Try to send the ICMP packet to the destination address.
			if (sendto(sock, buffer, (sizeof(icmp6_header) + payload_size), 0, (struct sockaddr *)&destination_address_v6, sizeof(destination_address_v6)) <= 0)
			{
				perror("ipv6 sendto(2)");
				close(sock);
				return 1;
			}

			// Poll the socket to wait for the ICMP reply packet.
			int ret = poll(fds, 1, TIMEOUT);

			// The poll(2) function returns 0 if the socket is not ready for reading after the timeout.
			if (ret == 0)
			{
				if (++retries == MAX_RETRY)
				{
					fprintf(stderr, "Request timeout for icmp_seq %d, aborting.\n", seq);
					break;
				}

				fprintf(stderr, "Request timeout for icmp_seq %d, retrying...\n", seq);
				--seq; // Decrement the sequence number to send the same request again.
				continue;
			}

			// The poll(2) function returns a negative value if an error occurs.
			else if (ret < 0)
			{
				perror("poll(2)");
				close(sock);
				return 1;
			}

			// Now we need to check if the socket actually has data to read.
			if (fds[0].revents & POLLIN)
			{
				// Temporary structure to store the source address of the ICMP reply packet.			
				struct sockaddr_in6 source_address_v6;
				

				// Zero out the buffer and the source address structure to make sure there are no garbage values.
				memset(buffer, 0, sizeof(buffer));
				memset(&source_address_v6, 0, sizeof(source_address_v6));

				// Try to receive the ICMP reply packet from the destination address.
				// Shouldn't fail, as we already checked if the socket is ready for reading.
				ssize_t bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0, 
									(struct sockaddr *)&source_address_v6, 
									&(socklen_t){sizeof(source_address_v6)});
				if (bytes_received > 0) {
				packets_received++; // Increment received packets counter
				}
				else if (bytes_received <= 0)
				{
					perror("recvfrom(2)");
					close(sock);
					return 1;
				}

				// Reset the number of retries to 0, as we received a reply packet.
				retries = 0;

				// Calculate the time it takes to send and receive the packet.
				gettimeofday(&end, NULL);

				// Start to extract the IP header and the ICMP header from the received packet.
				struct ip6_hdr *ip6_header = (struct ip6_hdr *)buffer;

				// ICMP header is located after the IP header, so we need to skip the IP header.
				// IP header size is determined by the ihl field, which is the first 4 bits of the header.
				// The ihl field is the number of 32-bit words in the header, so we need to multiply it by 4 to get the size in bytes.
				struct icmp6_hdr *icmp6_header = (struct icmp6_hdr *)(buffer + sizeof(struct ip6_hdr));

				// Check if the ICMP packet is an ECHO REPLY packet.
				// We may receive other types of ICMP packets, so we need to check the type field.
				if (icmp6_header->icmp6_type== ICMP6_ECHO_REPLY)
				{
					// Calculate the time it takes to send and receive the packet.
					float pingPongTime = ((float)(end.tv_usec - start.tv_usec) / 1000) + ((end.tv_sec - start.tv_sec) * 1000);
					//setting total time to be the sum of the all times.
					total_time += pingPongTime;
					// setting rtt min by the minimum time
					update_rtt_stats(pingPongTime);
					// Print the result of the ping request.
					// The result includes the number of bytes received (the payload size),
					// the destination IP address, the sequence number, the TTL value, and the time it takes to send and receive the packet.
					fprintf(stdout, "%ld bytes from %s: icmp_seq=%d hop_limit=%d time=%.2fms\n",
							(ntohs(ip6_header->ip6_plen) - sizeof(struct icmp6_hdr)), // Payload length minus ICMPv6 header
							inet_ntop(AF_INET6, &source_address_v6.sin6_addr, address, sizeof(address)), // Convert IPv6 address
							ntohs(icmp6_header->icmp6_seq), // ICMPv6 sequence number
							ip6_header->ip6_hlim, // Hop limit (equivalent to TTL in IPv4)
							pingPongTime);

					// Optional: Break the loop after a certain number of requests.
					// This will make the ping work like Windows' ping, which sends 4 requests by default.
					// Linux default ping uses infinite requests.
					if (seq == MAX_REQUESTS)
						break;
				}

				// If the ICMP packet isn't an ECHO REPLY packet, we need to print an error message.
				else
					fprintf(stderr, "Error: packet received with type %d\n", icmp6_header->icmp6_type);
			}

			// Sleep for 1 second before sending the next request.
			count--;
			
			if (flood == 0)
			{
				sleep(SLEEP_TIME);
			}
		// Close the socket and return 0 to the operating system.

		}
	}
	// Cleanup: Close the socket and display statistics
	close(sock);
	display_statistics(SIGINT);
	return 0;
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


unsigned short int calculate_checksum_icmp6(void *data, unsigned int bytes,
                                            struct in6_addr *src, struct in6_addr *dest, uint8_t next_header) {
    struct pseudo_header {
        struct in6_addr src_addr;
        struct in6_addr dest_addr;
        uint32_t length;
        uint8_t zeros[3];
        uint8_t next_header;
    } psh;

    memset(&psh, 0, sizeof(psh));
    psh.src_addr = *src;
    psh.dest_addr = *dest;
    psh.length = htonl(bytes);
    psh.next_header = next_header;

    unsigned int total_sum = 0;
    unsigned short int *data_pointer = (unsigned short int *)&psh;

    // Add pseudo-header to checksum
    for (unsigned int i = 0; i < sizeof(psh) / 2; i++) {
        total_sum += *data_pointer++;
    }

    // Add ICMPv6 header and payload to checksum
    data_pointer = (unsigned short int *)data;
    while (bytes > 1) {
        total_sum += *data_pointer++;
        bytes -= 2;
    }

    // Add left-over byte, if any
    if (bytes > 0) {
        total_sum += *((unsigned char *)data_pointer);
    }

    // Fold 32-bit sum to 16 bits
    while (total_sum >> 16) {
        total_sum = (total_sum & 0xFFFF) + (total_sum >> 16);
    }

    // Return the one's complement of the result
    return (~((unsigned short int)total_sum));
}

void display_statistics(int signum)
{
	fprintf(stdout, "\n--- %s ping statistics ---\n",address);
	fprintf(stdout, "%d packets transmitted, %d received , time %.2fms\n",seq,packets_received,total_time);
	if (packets_received > 0) {
        float avg_rtt = rtt_sum / packets_received; // Calculate average RTT
        float mdev = calculate_mdev();             // Calculate mean deviation
        fprintf(stdout, "rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n",
                rtt_min, avg_rtt, rtt_max, mdev);
    } else {
        fprintf(stdout, "No packets received, no RTT statistics available.\n");
    }
	close(sock);
    exit(1);
}

void update_rtt_stats(float rtt) {
    if (rtt_min < 0 || rtt < rtt_min) {
        rtt_min = rtt;
    }
    if (rtt_max < 0 || rtt > rtt_max) {
        rtt_max = rtt;
    }
    rtt_sum += rtt;
    rtt_sum_squares += rtt * rtt;
}

// Function to calculate mdev
float calculate_mdev() {
    if (packets_received <= 1) {
        return 0.0; // No deviation with fewer than 2 packets
    }
    float mean = rtt_sum / packets_received;
    float variance = (rtt_sum_squares / packets_received) - (mean * mean);
    return sqrt(variance);
}

int is_valid_option(const char* option) {
    const char* valid_options[] = {"-a", "-t", "-c", "-f"};
    int num_options = sizeof(valid_options) / sizeof(valid_options[0]);

    for (int i = 0; i < num_options; i++) {
        if (strcmp(option, valid_options[i]) == 0) {
            return 1; // Valid option
        }
    }
    return 0; // Invalid option
}
