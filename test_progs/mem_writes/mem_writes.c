#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


int main (int argc, char* argv[]){
    
    uint8_t **array;
    uint32_t row, col;
    uint32_t i, j, k;
    uint8_t temp[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    int temp_index = 0;

    if(argc != 3){
        printf("Error!\n");
        exit(-1);
    }

    row = atoi(argv[1]);
    col = atoi(argv[2]);

    array = (uint8_t**)malloc(sizeof(int*)*row);

    if(array == NULL){
        printf("Not enough memory\n");
        exit(-1);
    }

    for(i = 0; i < row; i++){
        temp_index = 0;
        array[i] = (uint8_t*)malloc(sizeof(uint8_t)*col);
        if(array[i] == NULL){
            printf("Not enough memory\n");
            exit(-1);
        }
        for(j = 0; j < col;){
            for (k = 0; k < 8; k++){
                array[i][j] = temp[temp_index] + k;
                j++;
            }
            temp_index++;
        }
        printf("ROW : %-7d INIT\n",i);
    }

    for(i = 0; i < row; i++){
        printf("ROW : %-7d DATA : ",i);
        for(j = 0; j < col; j++){
            printf("%x ", array[i][j]);
        }
        printf("\n");
    }

    


    return 1;
}
