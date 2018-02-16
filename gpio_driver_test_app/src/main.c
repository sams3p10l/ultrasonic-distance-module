#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_LEN 80

int enable;
double IIRiterations[2], result[2];
double alpha = 1;
char c;
int iir_cnt = 0;
int counter = 0;

double IIR(double d)
{
	if(iir_cnt != 3)
		iir_cnt++;
	if((iir_cnt) == 1)
	{
		return d;
	}
	else if(iir_cnt == 2)
	{
		result[0] = d;
		IIRiterations[0] = d;
		return result[0];
	}
	IIRiterations[1] = d;
	result[1] = alpha * (result[0] + IIRiterations[1] - IIRiterations[0]);
    IIRiterations[0] = IIRiterations[1];
    result[0] = result[1];

    return result[1];
}

int main()
{
    int file_desc;
    int a;
    char tmp[BUF_LEN];
    double distance, filteredDistance;

    IIRiterations[0] = 0;
	result[0] = 0;
	result[1] = 0;

    printf("Press ENTER to start measuring\n");
    while(1)
    {
        c = getchar();
		if(c == 10)
			break;
    }

    while (1)
    {
        file_desc = open("/dev/gpio_driver", O_RDWR);
		if(file_desc < 0)
		{
			printf("Error, 'output' not opened\n");
			return -1;
		}

		memset(tmp, 0, BUF_LEN);

        if(read(file_desc, tmp, BUF_LEN) != -1)
        {
            a = atoi(tmp);
            distance = (double)a/58;
			counter ++;
        }
        else
        {
            printf("File read error\n");
            return -1;
        }

		filteredDistance = IIR(distance);
		close(file_desc);

		if(counter == 100)
		{
			printf("Distance: %.2lf cm\n", filteredDistance);
			counter = 0;
		}

		usleep(500);
    }

    return 0;
}
