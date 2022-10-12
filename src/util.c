#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <linux/socket.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <termios.h>

#include <rte_timer.h>
#include <rte_ethdev.h>
#include <rte_ether.h>

#include "util.h"

void 
LOG_LINE(int len, char ch, char* info) {
    if (len <= 0) {
        struct winsize win;
        ioctl(STDIN_FILENO, TIOCGWINSZ, &win);
        len = win.ws_col - 5;
    }

    printf("[%sINFO%s] ", CO_GREEN,  CO_RESET);
    if (info == NULL) {
        for (int loop = 0; loop < len; loop++) {
            printf("%c", ch);
        }
        printf("\n");
        return;
    }
    if (len < strlen(info))
        len = strlen(info) + 10;
    int remain = len - strlen(info);
    int left   = remain / 2;
    int right  = remain - left;
    for (int loop = 1; loop < left; loop++)
        printf("%c", ch);
    printf(" %s%s%s ", CO_GREEN, info, CO_RESET);
    for (int loop = 1; loop < right; loop++)
        printf("%c", ch);
    printf("\n");
}

int
nic_getname_by_ip(char* ip_addr, char** nic_name) {
    struct in_addr addr;
    if(inet_aton(ip_addr, &addr) == 0)
        return -1;

    struct ifaddrs* if_list;
    if (getifaddrs(&if_list) < 0)
        return -1;

    *nic_name = NULL;
    for (struct ifaddrs *ifa = if_list; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET) {
            if (!memcmp(&addr, &(((struct sockaddr_in *) ifa->ifa_addr)->sin_addr), sizeof(struct in_addr))) {
                *nic_name = (char*) calloc(IFNAMSIZ, sizeof(char));
                strcpy(*nic_name, ifa->ifa_name);
                break;
            }
        }
    }
    freeifaddrs(if_list);

    if (*nic_name == NULL)
        return -1;
    return 0;
}

int 
nic_getip_by_name(const char *nic_name, char *ip) {
    struct sockaddr_in sin;
    struct ifreq ifr;
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sd) {
        LOG_ERRO("Cannot create socket (file=%s line=%d func=%s)\n", __FILE__, __LINE__, __func__);
        return -1;
    }

    strncpy(ifr.ifr_name, nic_name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;

    if (ioctl(sd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl");
        close(sd);
        return -1;
    }

    memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
    sprintf(ip, "%s", inet_ntoa(sin.sin_addr));

    close(sd);
    return 0;
}

int 
nic_getbusinfo_by_name(const char* nic_name, char** businfo) {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);

    struct ifreq ifr;
    struct ethtool_cmd cmd;
    struct ethtool_drvinfo drvinfo;

    memset(&ifr, 0, sizeof ifr);
    memset(&cmd, 0, sizeof cmd);
    memset(&drvinfo, 0, sizeof drvinfo);
    strcpy(ifr.ifr_name, nic_name);

    ifr.ifr_data = (void*) &drvinfo;
    drvinfo.cmd = ETHTOOL_GDRVINFO;

    if(ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
        perror("ioctl");
        close(sock);
        return -1;
    }
    *businfo = (char*) calloc(20, sizeof(char));
    strcpy(*businfo, drvinfo.bus_info);
    close(sock);
    return 0;
}

int 
nic_getnumanode_by_businfo(const char* businfo) {
    int numa_node = -1;
    char path[50] = {0};
    sprintf(path, "/sys/bus/pci/devices/%s/numa_node", businfo);
    FILE* fp = fopen(path, "r"); 
    if (fp != NULL) {
        char temp[5] = {0};
        char* ptr = fgets(temp, 5, fp);        
        numa_node = atoi(temp);
        fclose(fp);
    }
    return numa_node;
}

int
nic_getcpus_by_numa(int numa_node, char** cpu_list) {
    char match[10] = {0};
    sprintf(match, "node%d", numa_node);

    *cpu_list = NULL;

    int ignored = 0;
    FILE* fp = popen("lscpu", "r");
    if (fp != NULL) {
        char buff [256] = {0};
        while (fscanf(fp, "%s", buff) != EOF) {
            if (strncmp(buff, "NUMA", strlen("NUMA")) == 0) {
                ignored = fscanf(fp, "%s", buff);
                if (strncmp(buff, match, strlen(match)) == 0) {
                    ignored = fscanf(fp, "%s", buff);
                    ignored = fscanf(fp, "%s", buff);
                    *cpu_list = (char*) calloc(128, sizeof(char));
                    strcpy(*cpu_list, buff);
                    break;
                }
            }
        }
        fclose(fp);
    }

    if (*cpu_list == NULL) {
        return -1;
    }
    return 0;
}

struct rte_ether_addr 
nic_getarp_by_ip(char* ip_addr) {
    struct rte_ether_addr ret = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    int success = -1;
    FILE* fp = fopen("/proc/net/arp", "r");
    char buff [256] = {0};
    if (fp != NULL) {
        int ignored = 0;
        while (fscanf(fp, "%s", buff) != EOF) {
            if (strcmp(buff, ip_addr) == 0) {
                ignored = fscanf(fp, "%s", buff); // skip 'HW type'
                ignored = fscanf(fp, "%s", buff); // skip 'Flags'
                ignored = fscanf(fp, "%s", buff); 
                success = 0;
                break;
            }
        }
        fclose(fp);
    }

    int idx = 0;
    if (success == 0) {
        char* ptr = strtok(buff, ":");
        while (ptr != NULL) {
            uint8_t temp = 0;
            for (int loop = 0; loop < 2; loop++) {
                if ('0' <= ptr[loop] && ptr[loop] <= '9') {
                    temp = temp * 16 + ptr[loop] - '0';
                } else {
                    temp = temp * 16 + ptr[loop] - 'a' + 10;
                }
            }
            ret.addr_bytes[idx++] = temp;
            ptr = strtok(NULL, ":");
        }
    }
    return ret;
}

int 
cpu_getmask(char* cpu_list, int num_core, char** core_mask) {
    int allocated_core = 0;
    char str_temp[256] = {0};
    strcpy(str_temp, cpu_list);

    int index = 0;
    char cores[64][32] = {0};

    char* delim = ",";
    char* ptr = strtok(str_temp, delim);
    while (ptr != NULL) {
        strcpy(cores[index++], ptr);
        ptr = strtok(NULL, delim);
    }

    int map[1024] = {0};
    int max_core = 1024;
    for (int loop = 0; loop < index; loop++) {
        int left = 0, right = 0;
        ptr = strtok(cores[loop], "-");
        left = atof(ptr);
        ptr = strtok(NULL, "-");
        right = atof(ptr);
        for (int idx = left; idx <= right; idx++) {
            if (idx >= max_core)
                break;
            if (num_core > 0) {
                map[idx] = 1;
                num_core = num_core - 1;
            }
        }
    }

    *core_mask = (char* ) calloc(5+max_core/4, sizeof(char));
    int mask_idx = 0;
    (*core_mask)[mask_idx++] = '0';
    (*core_mask)[mask_idx++] = 'x';
    bool start = false;
    for (int loop = max_core/4 - 1; loop >= 0; loop--) {
        int sum = 0;
        for (int inner = 0; inner < 4; inner++) {
            if (map[loop*4 + inner] == 1) {
                allocated_core += 1;
                sum = sum + (1 << inner);
            }
        }
        if (sum == 0 && start == false) {
            continue;
	} else if (0 <= sum && sum <= 9) {
            (*core_mask)[mask_idx++] = (char) (sum + '0');
        } else {
            (*core_mask)[mask_idx++] = (char) (sum - 10 + 'a');
        }
	start = true;
    }

    return allocated_core;
}

bool 
has_suffix(char* str, char* suf) {
    int n1 = strlen(str), n2 = strlen(suf);
    if (n1 < n2)
        return false;
    for (int i = 0; i < n2; i++)
       if (str[n1 - i - 1] != suf[n2 - i - 1])
           return false;
    return true;
}

int 
time_strto_tv(char* time, struct timeval* tv) {
    int temp = atoi(time);
    int success = 0;
    if (has_suffix(time, "us") == true) {
        tv->tv_sec  = temp / 1000000;
        tv->tv_usec = temp % 1000000;
    } else if (has_suffix(time, "ms") == true) {
        temp = temp * 1000;
        tv->tv_sec  = temp / 1000000;
        tv->tv_usec = temp % 1000000;
    } else if (has_suffix(time, "s") == true) { 
        tv->tv_sec  = temp;
        tv->tv_usec = 0;
    } else if (has_suffix(time, "m") == true) { 
        tv->tv_sec  = temp * 60;
        tv->tv_usec = 0;
    } else if (has_suffix(time, "h") == true) { 
        tv->tv_sec  = temp * 60 * 60;
        tv->tv_usec = 0;
    } else {
        success = -1;
    }
    return success;
}

uint64_t time_to_hz_tv(struct timeval t) {
    uint64_t ret = 0;
    uint64_t hz = rte_get_timer_hz();

    ret = time_double(t) * hz;
    return ret;
}
uint64_t time_to_hz_s(uint32_t t) {
    uint64_t ret = 0;
    uint64_t hz = rte_get_timer_hz();

    ret = t * hz;
    return ret;
}
uint64_t time_to_hz_ms(uint32_t t) {
    uint64_t ret = 0;
    uint64_t hz = rte_get_timer_hz();

    ret = t * hz / 1000;
    return ret;
}
uint64_t time_to_hz_us(uint32_t t) {
    uint64_t ret = 0;
    uint64_t hz = rte_get_timer_hz();

    ret = t * hz / 1000000;
    return ret;
}
uint64_t hz_to_us(uint64_t t) {
    uint64_t hz = rte_get_timer_hz();
    return t * 1000000 / hz;
}
uint64_t hz_to_ms(uint64_t t) {
    uint64_t hz = rte_get_timer_hz();
    return t * 1000 / hz;
}
uint64_t hz_to_s(uint64_t t) {
    uint64_t hz = rte_get_timer_hz();
    return t / hz;
}
struct timeval hz_to_tv(uint64_t t) {
    struct timeval ret = {0};
    uint64_t us = hz_to_us(t);
    ret.tv_sec = us / 1000000;
    ret.tv_usec= us % 1000000;
    return ret;
}
