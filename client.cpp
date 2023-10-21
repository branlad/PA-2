#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <iostream>

#include "packet.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 5) {
        cerr << "Usage: client <emulatorName> <sendToEmulator> <receiveFromEmulator> <fileName>" << endl;
        return 1;
    }

    struct hostent *em_host = gethostbyname(argv[1]);
    if (em_host == NULL) {
        cerr << "Failed to obtain server's hostname." << endl;
        exit(EXIT_FAILURE);
    }

    int CESocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (CESocket < 0) {
        perror("Failed to open datagram socket");
        exit(EXIT_FAILURE);
    }

    sockaddr_in CE;
    socklen_t CE_length = sizeof(CE);
    memset(&CE, 0, sizeof(CE));
    CE.sin_family = AF_INET;
    memcpy(&CE.sin_addr, em_host->h_addr, em_host->h_length);
    CE.sin_port = htons(stoi(argv[2]));

    int ECSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ECSocket < 0) {
        perror("Failed to open datagram socket");
        exit(EXIT_FAILURE);
    }

    sockaddr_in EC;
    socklen_t EC_length = sizeof(EC);
    memset(&EC, 0, sizeof(EC));
    EC.sin_family = AF_INET;
    EC.sin_addr.s_addr = htonl(INADDR_ANY);
    EC.sin_port = htons(stoi(argv[3]));

    if (bind(ECSocket, (struct sockaddr *)&EC, EC_length) == -1) {
        perror("Error in binding");
        exit(EXIT_FAILURE);
    }

    ifstream input_file(argv[4], ios::binary);
    ofstream seqnum_log("clientseqnum.log");
    ofstream ack_log("clientack.log");

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (setsockopt(ECSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Error setting socket options");
        exit(EXIT_FAILURE);
    }

    char file_buffer[30];
    int seq = 0;

    while (!input_file.eof()) {
        input_file.read(file_buffer, sizeof(file_buffer));
        int num_read = input_file.gcount();

        packet data_packet(1, seq, num_read, file_buffer);
        seqnum_log << seq << endl;

        char serialized[512];
        data_packet.serialize(serialized);

        cout << "Sending packet with sequence number: " << seq << endl;
        data_packet.printContents();

        while (true) {
            if (sendto(CESocket, serialized, sizeof(serialized), 0, (struct sockaddr *)&CE, CE_length) < 0) {
                perror("Error sending data");
                continue;
            }

            int n = recvfrom(ECSocket, serialized, sizeof(serialized), 0, (struct sockaddr *)&EC, &EC_length);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    cout << "Timeout occurred. Resending packet with sequence number: " << seq << endl;
                    continue;
                } else {
                    perror("Error receiving data");
                    break;
                }
            }

            char data[512];
            memset(data, 0, sizeof(data));
            packet ack_packet(0, 0, 0, data);
            ack_packet.deserialize(serialized);

            ack_log << ack_packet.getSeqNum() << endl;

            if (ack_packet.getSeqNum() == seq) {
                seq = 1 - seq;
                break;
            }
        }
    }

    packet eot_packet(3, seq, 0, nullptr);
    char eot_serialized[512];
    eot_packet.serialize(eot_serialized);
    sendto(CESocket, eot_serialized, sizeof(eot_serialized), 0, (struct sockaddr *)&CE, CE_length);
    seqnum_log << seq << endl;

    char eot_received_serialized[512];
    int n = recvfrom(ECSocket, eot_received_serialized, sizeof(eot_received_serialized), 0, (struct sockaddr *)&EC, &EC_length);
    if (n > 0) {
        packet eot_received(0, 0, 0, nullptr);
        eot_received.deserialize(eot_received_serialized);
        if (eot_received.getType() == 2) {
            cout << "Received EOT from server. Transmission complete." << endl;
        }
        ack_log << eot_received.getSeqNum() << endl;
    }

    input_file.close();
    seqnum_log.close();
    ack_log.close();
    close(CESocket);
    close(ECSocket);
    return 0;
}
