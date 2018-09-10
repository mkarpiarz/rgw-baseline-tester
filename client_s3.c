#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <time.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

unsigned char* hmac_sha256(const void *key, int keylen,
                           const unsigned char *data, int datalen,
                           unsigned char *result, unsigned int* resultlen)
{
    return HMAC(EVP_sha256(), key, keylen, data, datalen, result, resultlen);
}

void to_hex(const unsigned char *source, const unsigned int source_len, char *result, unsigned int result_len)
{
    char res[2*source_len];
    int i;
    for (i = 0; i < source_len; ++i)
        sprintf(&result[i*2], "%02x", source[i]);
    result = res;
    result_len = strlen(res);
}

int main(int argc, char *argv[])
{
    if (argc < 9)
    {
        fprintf(stderr,"usage %s hostname port bucket object-name object-size object-hash num-objects send-only\n", argv[0]);
        fprintf(stderr,"\t<bucket> - name of an existing bucket\n");
        fprintf(stderr,"\t<object-name> - name for object in RGW (will be created)\n");
        fprintf(stderr,"\t<object-size> - size (in B) of the new object\n");
        fprintf(stderr,"\t<object-hash> - sha256 checksum of the object to send\n");
        fprintf(stderr,"\t<num-objects> - number of objects to send\n");
        fprintf(stderr,"\t<send-only> - set to 1 to ignore responses from server, 0 otherwise\n");
        exit(0);
    }

    const char *host = argv[1];
    const int portno = atoi(argv[2]);
    printf("host: %s\n", host);
    printf("port: %d\n", portno);
    const char *bucket = argv[3];
    const char *object_name = argv[4];
    const long unsigned int object_size = atoi(argv[5]);
    // SHA256 of an empty string = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    const char *payload_hash = argv[6];
    const long unsigned int n_objects = atoi(argv[7]);
    const short sendonly = atoi(argv[8]);

    if (sendonly)
        fprintf(stderr, "INFO: send-only mode enabled.\n");

    char creds_filename[512];
    const char *homedir = getenv("HOME");
    if (homedir != NULL)
        sprintf(creds_filename, "%s/.aws/credentials", homedir);
    else
        strcpy(creds_filename, "credentials");
    printf("credentials file: %s\n", creds_filename);
    FILE *creds_file;
    if ( !(creds_file = fopen(creds_filename, "r")) )
    {
        fprintf(stderr, "ERROR: Missing credentials file: %s\n", creds_filename);
        exit(1);
    }
    char key_id[256], key[256];
    fscanf(creds_file, "[default]\naws_access_key_id = %s\naws_secret_access_key = %s\n", key_id, key);
    fclose(creds_file);

    const time_t current_time = time(NULL); // current time
    char date_stamp[10];
    strftime( date_stamp, sizeof(date_stamp), "%Y%m%d", gmtime(&current_time) );
    printf("date_stamp: %s\n", date_stamp);

    const char *region_name = "us-east-1";
    const char *service_name = "s3";

    const char *method = "PUT";
    char path[256] = "/";
    strcat(path, bucket);
    strcat(path, "/");
    strcat(path, object_name);

    // Canonical request
    char now[17];
    strftime( now, sizeof(now), "%Y%m%dT%H%M%SZ", gmtime(&current_time) );
    printf("now: %s (%lu)\n", now, strlen(now));
    char canonical_request[4096];
    sprintf(canonical_request, "%s\n"
        "%s\n"
        "\n"
        "host:%s:%d\n"
        "x-amz-content-sha256:%s\n"
        "x-amz-date:%s\n"
        "\n"
        "host;x-amz-content-sha256;x-amz-date\n"
        "%s",
        method, path, host, portno, payload_hash, now, payload_hash);

    // Canonical request hash
    const unsigned char *canonical_request_digest = SHA256(canonical_request, strlen(canonical_request), NULL);
    char canonical_request_hash[2*SHA256_DIGEST_LENGTH];
    to_hex(canonical_request_digest, SHA256_DIGEST_LENGTH, canonical_request_hash, strlen(canonical_request_hash));

    // Policy string to sign
    char string_to_sign[1024];
    sprintf(string_to_sign, "AWS4-HMAC-SHA256\n"
        "%s\n"
        "%s/%s/%s/aws4_request\n"
        "%s",
        now, date_stamp, region_name, service_name, canonical_request_hash);


    char aws_key[128] = {'\0'};
    strcat(aws_key, "AWS4");
    strcat(aws_key, key);
    unsigned char *kdate_digest = NULL;
    unsigned int kdate_digest_len;
    kdate_digest = hmac_sha256( aws_key, strlen(aws_key), date_stamp, strlen(date_stamp), NULL, &kdate_digest_len );
    char kdate[2*kdate_digest_len];
    unsigned int kdate_len;
    to_hex(kdate_digest, kdate_digest_len, kdate, kdate_len);

    unsigned char *kregion_digest = NULL;
    unsigned int kregion_digest_len;
    kregion_digest = hmac_sha256( kdate_digest, kdate_digest_len, region_name, strlen(region_name), NULL, &kregion_digest_len );
    char kregion[2*kregion_digest_len];
    unsigned int kregion_len;
    to_hex(kregion_digest, kregion_digest_len, kregion, kregion_len);

    unsigned char *kservice_digest = NULL;
    unsigned int kservice_digest_len;
    kservice_digest = hmac_sha256( kregion_digest, kregion_digest_len, service_name, strlen(service_name), NULL, &kservice_digest_len );
    char kservice[2*kservice_digest_len];
    unsigned int kservice_len;
    to_hex(kservice_digest, kservice_digest_len, kservice, kservice_len);

    const char *ksigning_data = "aws4_request";
    unsigned char *ksigning_digest = NULL;
    unsigned int ksigning_digest_len;
    ksigning_digest = hmac_sha256( kservice_digest, kservice_digest_len, ksigning_data, strlen(ksigning_data), NULL, &ksigning_digest_len );
    char ksigning[2*ksigning_digest_len];
    unsigned int ksigning_len;
    to_hex(ksigning_digest, ksigning_digest_len, ksigning, ksigning_len);

    // Signature
    unsigned char *signature_digest = NULL;
    unsigned int signature_digest_len;
    signature_digest = hmac_sha256( ksigning_digest, ksigning_digest_len, string_to_sign, strlen(string_to_sign), NULL, &signature_digest_len );
    char signature[2*signature_digest_len];
    unsigned int signature_len;
    to_hex(signature_digest, signature_digest_len, signature, signature_len);

    // Authorisation header
    char auth_header[1024];
    sprintf(auth_header, "Authorization: AWS4-HMAC-SHA256"
            " Credential=%s/%s/%s/%s/aws4_request,"
            " SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
            " Signature=%s",
            key_id, date_stamp, region_name, service_name, signature);

    // Prepare headers
    char headers_to_send[4096];
    if (sendonly)
    {
        // Don't send "Expect: 100-Continue" when in send-only mode
        sprintf(headers_to_send, "%s %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "%s\r\n"
                "x-amz-content-sha256: %s\r\n"
                "x-amz-date: %s\r\n"
                "Content-Length: %lu\r\n"
                "\r\n",
                method, path, host, portno, auth_header, payload_hash, now, object_size);
    }
    else
    {
        sprintf(headers_to_send, "%s %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "%s\r\n"
                "x-amz-content-sha256: %s\r\n"
                "x-amz-date: %s\r\n"
                "Expect: 100-Continue\r\n"
                "Content-Length: %lu\r\n"
                "\r\n",
                method, path, host, portno, auth_header, payload_hash, now, object_size);
    }
    printf("\n== HEADERS ==\n");
    printf("%s\n", headers_to_send);


    char outbuf[1024];

    unsigned long i;
    for (i = 0; i < n_objects; ++i)
    {
        printf("INFO: Sending object %lu...\n", i+1);
        // Send through a TCP socket
        struct sockaddr_in serv_addr;
        struct hostent *server;
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
        {
            perror("ERROR opening socket");
            exit(1);
        }
        server = gethostbyname(argv[1]);
        if (server == NULL)
        {
            fprintf(stderr,"ERROR, no such host\n");
            exit(1);
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr,
             (char *)&serv_addr.sin_addr.s_addr,
             server->h_length);
        serv_addr.sin_port = htons(portno);

        // Prepare data
        // Create object on the heap to bypass stack size limitations
        char *object_content = (char*) malloc(object_size+1);
        memset(object_content, '*', object_size*sizeof(char));

        if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        {
            perror("ERROR connecting");
            free(object_content);
            exit(1);
        }
        // Send headers
        int n = write(sockfd, headers_to_send, strlen(headers_to_send));
        if (n < 0)
        {
            perror("ERROR writing to socket");
            free(object_content);
            exit(1);
        }
        if (!sendonly)
        {
            // Get response (100 Continue)
            n = read(sockfd, outbuf, sizeof(outbuf));
            if (n < 0)
            {
                perror("ERROR reading data from socket");
                free(object_content);
                exit(1);
            }
        }
        // Send data
        n = write(sockfd, object_content, object_size);
        if (n < 0)
        {
            perror("ERROR writing data to socket");
            free(object_content);
            exit(1);
        }
        if (!sendonly)
        {
            // Wait for final response (200 OK)
            n = read(sockfd, outbuf, sizeof(outbuf));
            if (n < 0)
            {
                perror("ERROR reading data from socket");
                free(object_content);
                exit(1);
            }
        }
        free(object_content);
        close(sockfd);
        printf("INFO: Object %lu sent.\n", i+1);
    }

    return 0;
}
