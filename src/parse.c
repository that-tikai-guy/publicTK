#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "parse.h"



int create_db_header(struct dbheader_t **headerOut) {
    struct dbheader_t *header = calloc(1, sizeof(struct dbheader_t));
    if (header == NULL) {
        printf("MemAlloc failed creating database header!\n");
        return STATUS_ERROR;
    }

    header->version  = 0x1;
    header->count    = 0;
    header->magic    = HEADER_MAGIC;
    header->filesize = sizeof(struct dbheader_t);

    *headerOut = header;

    return STATUS_SUCCESS;
}

int validate_db_header(int fd, struct dbheader_t **headerOut) {
    if (fd < 0) {
        printf("Bad FD input!\n");
        return STATUS_ERROR;
    }

    struct dbheader_t *header = calloc(1, sizeof(struct dbheader_t));
    if (header == NULL) {
        printf("MemAlloc failed creating database header!\n");
        return STATUS_ERROR;
    }

    if (read(fd, header, sizeof(struct dbheader_t)) != sizeof(struct dbheader_t)) {
        perror("read");
        free(header);
        return STATUS_ERROR;
    }

    header->magic    = ntohl(header->magic);
    header->version  = ntohs(header->version);
    header->count    = ntohs(header->count);
    header->filesize = ntohl(header->filesize);

    if (header->magic != HEADER_MAGIC) {
        printf("Header magic corrupted!\n");
        free(header);
        return -1;
    }

    if (header->version != 1) {
        printf("Header version corrupted!\n");
        free(header);
        return -1;
    }

    struct stat dbstat = {0};
    fstat(fd, &dbstat);
    if (header->filesize != dbstat.st_size) {
        printf("Database corrupted!\n");
        free(header);
        return -1;
    }

    *headerOut = header;

    return STATUS_SUCCESS;
}

int read_employees(int fd, struct dbheader_t *dbhdr, struct employee_t **employeesOut) {
    if (fd < 0) {
        printf("Bad FD input!\n");
        return STATUS_ERROR;
    }

    int count = dbhdr->count;

    struct employee_t *employees = calloc(count, sizeof(struct employee_t));
    if (employees == NULL) {
        printf("MemAlloc failed creating employees buffer!\n");
        return STATUS_ERROR;
    }

    if (read(fd, employees, count*sizeof(struct employee_t)) != count*sizeof(struct employee_t)) {
        perror("read");
        free(employees);
        return STATUS_ERROR;
    } else {
        int i = 0;
        for (; i < count; i++) {
            employees[i].hours = ntohl(employees[i].hours);
        }

        *employeesOut = employees;
        return STATUS_SUCCESS;
    }
}

int add_employee(struct dbheader_t *dbhdr, struct employee_t *employees, char *addstring) {
    printf("%s\n", addstring);

    char *name = strtok(addstring, ",");

    char *address = strtok(NULL, ",");

    char *hours = strtok(NULL, ",");

    printf("%s %s %s\n", name, address, hours);

    strncpy(employees[dbhdr->count-1].name, name, sizeof(employees[dbhdr->count-1].name));
    strncpy(employees[dbhdr->count-1].address, address, sizeof(employees[dbhdr->count-1].address));
    employees[dbhdr->count-1].hours = atoi(hours);

    return STATUS_SUCCESS;
}

int output_file(int fd, struct dbheader_t *dbhdr, struct employee_t *employees) {
    if (fd < 0) {
        printf("Bad FD input!\n");
        return STATUS_ERROR;
    }

    int realcount = dbhdr->count;

    dbhdr->magic    = htonl(dbhdr->magic);
    dbhdr->version  = htons(dbhdr->version);
    dbhdr->count    = htons(dbhdr->count);
    dbhdr->filesize = htonl(sizeof(struct dbheader_t) + (sizeof(struct employee_t) * realcount));

    lseek(fd, 0, SEEK_SET);

    write(fd, dbhdr, sizeof(struct dbheader_t));

    int i = 0;
    for (; i < realcount; i++) {
        employees[i].hours = htonl(employees[i].hours);
        write(fd, &employees[i], sizeof(struct employee_t));
    }

    return STATUS_SUCCESS;
}
