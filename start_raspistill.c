//----------------------------------------------------------------------------------------
// Code to launch raspistill as a separately running process
//----------------------------------------------------------------------------------------
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
    typedef int pid_t;
    #define execvp(a,b)
    #define fork() 1
#else
    #include <unistd.h>
#endif

#include "imgcomp.h"
#include "jhead.h"

//"raspistill -q 10 -n -bm -th none -p 480,0,800,480 -w 1280 -h 720 -o /ramdisk/out%05d.jpg -t 4000000 -tl 300";

static int raspistill_pid = 0;
extern int NightMode;

//-----------------------------------------------------------------------------------
// Parse command line and launch.
//-----------------------------------------------------------------------------------
static void do_launch_program(void)
{
    char * Arguments[51];
    int narg;
    int a;
    int wasblank = 1;

    // Note: NOT handling quoted strings or anything for arguments with spaces in them.
    narg=0;
    for (a=0;raspistill_cmd[a];a++){
        if (raspistill_cmd[a] != ' '){
            if (wasblank){
                if (narg >= 50){
                    fprintf(stderr, "raspistill too many raspistill arguments\n");
                    exit(0);
                }
                Arguments[narg++] = &raspistill_cmd[a];
            }
            wasblank = 0;
        }else{
            raspistill_cmd[a] = '\0';
            wasblank = 1;
        }
    }
    Arguments[narg] = NULL;

    //printf("%d arguments\n",narg);
    //for (a=0;a<narg;a++){
    //    printf("'%s'\n",Arguments[a]);
    //}
    execvp(Arguments[0], Arguments);

    // execvp only returns if there is an error.

    fprintf(stderr,"Failed to execute: %s\n",Arguments[0]);
    perror("Reason");
    exit(errno);
}

//-----------------------------------------------------------------------------------
// Parse command line and launch.
//-----------------------------------------------------------------------------------
int launch_raspistill(void)
{
    pid_t pid;
    int ignore;

    // Kill raspistill if it's already running.
    ignore = system("killall raspistill");
    ignore += 1;  // Do something with it to supress warning

    printf("Launching raspistill program\n");
    pid = fork();
    if (pid == -1){
        // Failed to fork.
        fprintf(stderr,"Failed to fork off child process\n");
        perror("Reason");
        return -1;
    }

    if(pid == 0){ 
        // Child takes this branch.
        do_launch_program();
    }else{
        raspistill_pid = pid;
    }
    return 0;
}


//-----------------------------------------------------------------------------------
// Parse command line and launch.
//-----------------------------------------------------------------------------------
int SecondsSinceImage = 0;
int SecondsSinceLaunch = 0;
int InitialAverageBright;
int InitialBrSum;
int InitialNumBr;
double RunningAverageBright;

void manage_raspistill(int NewImages)
{
    SecondsSinceImage += 1;
    SecondsSinceLaunch += 1;
    if (NewImages > 0){
        SecondsSinceImage = 0;
        printf("Exp:%5.1fms Iso:%d  Nm=%d  Bright:%d  av=%5.2f\r",
            ImageInfo.ExposureTime*1000, ImageInfo.ISOequivalent, 
            NightMode, NewestAverageBright, RunningAverageBright);
        fflush(stdout);
    }else{
        printf("No new images, %d\n",SecondsSinceImage);
    }

    if (raspistill_pid == 0){
        // Raspistill has not been launched.
        printf("Initial launch of raspistill\n");
        goto force_restart;
    }

    if (SecondsSinceImage > 10){
        // Not getting any images for 10 seconds.  Probably something went wrong with raspistill.
        printf("No images timeout.  Relaunch  raspistill\n");
        goto force_restart;
    }

    if (SecondsSinceLaunch > 36000){
        printf("1 hour raspistill relaunch\n");
        goto force_restart;
    }

    if (SecondsSinceLaunch > 5 && InitialNumBr < 5 && NewImages){
        printf("Average in %d\n",NewestAverageBright);
        InitialBrSum += NewestAverageBright;
        InitialNumBr += 1;
        // Save average brightness and reset averaging.
        if (InitialNumBr == 5){
            InitialAverageBright = (InitialBrSum+2) / 5;
            if (InitialAverageBright == 0) InitialAverageBright = 1; // Avoid division by zero.
            RunningAverageBright = InitialAverageBright;
            printf("Initial rightness average = %d\n",InitialAverageBright);
        }
    }

    // 20 second time constant brightness averaging.
    RunningAverageBright = RunningAverageBright * 0.95 + NewestAverageBright * 0.05;

    // If brightness changes by more than 20%, relaunch.
    if (SecondsSinceLaunch > 15){
        double Ratio;
        Ratio = RunningAverageBright / InitialAverageBright;
        if (Ratio < 1) Ratio = 1/Ratio;
        if (Ratio > 1.2){
            printf("Brightness change by 20%%.  Force restart\n");
            goto force_restart;
        }
    }

    // If image too bright and shutter speed is not fastest, launch raspistill
    // if image is too dark and shutter speed is not 1/8, launch raspistill.


    return;
force_restart:
    launch_raspistill();
    SecondsSinceImage = 0;
    SecondsSinceLaunch = 0;
    InitialBrSum = InitialNumBr = 0;

}
