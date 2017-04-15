#ifndef GUARD_EMU_H
#define GUARD_EMU_H
#pragma once

#define LOW 0
#define HIGH 1

#define OUTPUT 201
#define INPUT 200
#define PUD_OFF 300
#define INT_EDGE_BOTH 400

int Z_system(const char*);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
void wiringPiSetup();
void pinMode(int pin, int mode);
void pullUpDnControl(int pin, int pudMode);
void wiringPiISR(int pin, int edgeMode, void(*handler)());
struct Z_sysinfo
{
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
};
int Z_sysinfo(struct Z_sysinfo* si);

#endif
