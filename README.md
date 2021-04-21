Philip Wright
CMPSCI 4760 Project 5
Developed on Visual Studio
Added to GitHub on 4/20/2021

Little bit about my project:
The deadlock avoidance operates under the assumption that, contrary to instructions and based on what was discussed in class, the maximum claim of each worker process is 1 (instead of 1-3 per instructions).

The avoidance algorithm has some flaws. I assume the requests granted percentage is usually too high. The program normally finishes correctly but sometimes gets caught on one child remaining for an unknown reason. 
This can be dealt with by simply hitting ctrl-C until it catches the SIGINT.

Program can be compiled by typing 'make' from within the project folder. 
It can then be run by typing './oss'
This should output the required output to "logfile"

The output is by default verbose: on
