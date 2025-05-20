#!/usr/bin/env python3
# This program will toggle LEDs based on the number pressed
# 1 -> RED
# 2 -> GREEN
# 3 -> BLUE
from panda import Panda

# Number representation of LEDs in firmware
#define LED_RED 0U
#define LED_GREEN 1U
#define LED_BLUE 2U

##############    RED        GREEN        BLUE
out_colors = [ '\033[91m', '\033[92m', '\033[94m' ]

############# R  G  B
led_state = [ 0, 0, 0]

def usage():
    print('The program starts by settings all LEDs to 0')
    print('Press the following numbers to toggle LEDs:')
    print("\t1 -> Red")
    print("\t2 -> Green")
    print("\t3 -> Blue")

def print_leds():
    out=''
    for i in range(len(led_state)):
        out += f'{out_colors[i]}{i+1}: [{led_state[i]}] '
    print(f'{out}\033[0m')

def clear_leds():
    for i in range(len(led_state)):
        p.set_led(i, 0)

def toggle_led(led):
    led_num = led - 1
    led_state[led_num] ^= 1
    p.set_led(led_num, led_state[led_num])

if __name__ == '__main__':
    p = Panda()
    clear_leds()

    while True:
        print(chr(27) + "[2J") # clear screen
        print_leds() 
        led = int(input())
        if led < 1 or led > len(led_state):
            continue

        toggle_led(led)
     