#!/usr/bin/python

import sys

aglobal9 = 9

def celsius_to_farenheit(celsius):
    atuple = "a", "b", "c"
    alist = [1, 2, 3]
    aset = {1, 2, 3}
    adict = { 1 : "a", 2 : "b", 3 : "c" }

    nine = aglobal9
    five = 5
    thirty_two = 32
    i = 1
    return str((nine * celsius) / five + thirty_two)

def main():
    if (len(sys.argv) < 2):
        print ("Usage: " + sys.argv[0] + " Temp")
        return 1

    celsius = int(sys.argv[1])
    print (str(celsius) + " Celsius " + " is " + celsius_to_farenheit(celsius) + " Farenheit")
    return 0

if __name__ == "__main__":
    sys.exit(main())
