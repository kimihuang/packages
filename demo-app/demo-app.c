/*
 * Demo application for Quantum board
 *
 * A simple example application demonstrating
 * Buildroot external package infrastructure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERSION "1.0"

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -i <count>     Print greeting <count> times\n");
}

void print_greeting(int count) {
    for (int i = 0; i < count; i++) {
        printf("Hello from Quantum Board! (count: %d)\n", i + 1);
        usleep(100000); /* 100ms delay */
    }
}

int main(int argc, char *argv[]) {
    int count = 1;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("demo-app version %s\n", VERSION);
            printf("Built for Quantum Board\n");
            return 0;
        } else if (strcmp(argv[1], "-i") == 0) {
            if (argc > 2) {
                count = atoi(argv[2]);
                if (count < 1 || count > 100) {
                    fprintf(stderr, "Error: count must be between 1 and 100\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: -i requires a count argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("========================================\n");
    printf("  Demo Application v%s\n", VERSION);
    printf("  Quantum Board Example\n");
    printf("========================================\n\n");

    print_greeting(count);

    printf("\nDemo application completed successfully!\n");
    return 0;
}
