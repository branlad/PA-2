#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>

#include "packet.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 5) {
        cerr << "Usage: server <emulatorName> <receiveFromEmulator> <sendToEmulator-Port> <fileName>" << endl;
        return 1;
    }

    // Create a socket for receiving from the emulator
    int ESSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ESSocket < 0) {
        perror("Error: failed to open datagram socket.");
        return EXIT_FAILURE;
    }

    // Setup the sockaddr_in structure for receiving
    struct sockaddr_in ES;
    socklen_t ES_length = sizeof(ES);
    memset(&ES, 0, sizeof(ES));
    ES.sin_family = AF_INET;
    ES.sin_addr.s_addr = INADDR_ANY;
    ES.sin_port = htons(stoi(argv[2]));  // Convert string to integer directly with stoi

    if (bind(ESSocket, (struct sockaddr *)&ES, ES_length) == -1) {
        perror("Error in binding.");
        close(ESSocket);
        return EXIT_FAILURE;
    }

    // Setup for sending to the emulator
    struct sockaddr_in SE;
    memset(&SE, 0, sizeof(SE));
    SE.sin_family = AF_INET;
    SE.sin_port = htons(stoi(argv[3]));
    struct hostent *em_host = gethostbyname(argv[1]);

    if (!em_host) {
        cerr << "Failed to obtain emulator's hostname." << endl;
        close(ESSocket);
        return EXIT_FAILURE;
    }
    memcpy(&SE.sin_addr, em_host->h_addr_list[0], em_host->h_length);

    char payload[512];
    int expected_seq = 0;
    ofstream output_file(argv[4], ofstream::trunc);
    ofstream log_file("arrival.log", ofstream::trunc);

    while (true) {
        memset(payload, 0, sizeof(payload));
        char data[512];
        memset(data, 0, sizeof(data));
        packet received_packet(0, 0, 0, data);
        int bytesReceived = recvfrom(ESSocket, payload, sizeof(payload), 0, (struct sockaddr *)&ES, &ES_length);

        if (bytesReceived < 0) {
            perror("Error receiving data");
            continue;
        }

        received_packet.deserialize(payload);
        log_file << received_packet.getSeqNum() << endl;

        if (received_packet.getType() == 3) {  // EOT from client
            cout << "Received EOT from client. Sending EOT back..." << endl;
            packet eot_packet(2, expected_seq, 0, NULL);
            eot_packet.serialize(payload);
            if (sendto(ESSocket, payload, sizeof(payload), 0, (struct sockaddr *)&SE, sizeof(SE)) < 0) {
                perror("Error sending EOT");
            }
            break;
        } else if (received_packet.getSeqNum() == expected_seq) {
            cout << "Packet is in order. Writing to file and sending ACK..." << endl;
            output_file.write(received_packet.getData(), received_packet.getLength());
            expected_seq = 1 - expected_seq;  // toggle between 0 and 1
        } else {
            cout << "Out of order packet. Discarding and sending ACK of the most recent in-order packet..." << endl;
        }

        // Send ACK
        packet ack_packet(0, received_packet.getSeqNum(), 0, NULL);
        ack_packet.serialize(payload);
        if (sendto(ESSocket, payload, sizeof(payload), 0, (struct sockaddr *)&SE, sizeof(SE)) < 0) {
            perror("Error sending ACK");
        }
    }

    output_file.close();
    log_file.close();
    close(ESSocket);
    cout << "Server finished and exiting." << endl;

    return 0;
}
