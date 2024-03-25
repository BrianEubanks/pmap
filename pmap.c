#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/user.h>



// Page size 4K Bytes
#define PAGE_SIZE4      4096
#define PAGE_SHIFT4     12

// Page size 16K Bytes
#define PAGE_SIZE5      16384
#define PAGE_SHIFT5     14

#define PAGE_SIZE   (getpagesize())
#define PAGE_SHIFT  PAGE_SHIFT4     //Assume 4K

// Double word aligned
#define VAL_SIZE        sizeof(unsigned long)

// Pages to allocate
#define PG_COUNT        4096

#define PAGEMAP_LENGTH 8

#define BUF_SIZE_DW         (PAGE_SIZE/VAL_SIZE)*PG_COUNT
#define BUF_SIZE_BYTE       PAGE_SIZE*PG_COUNT


#define INIT 1

#define HAMMER_ROUND 1000


unsigned long* createBuffer(void) {
	size_t buf_size = BUF_SIZE_BYTE;

	//printf("pg: %x\n",PAGE_SIZE4);
	//printf("pg: %x\n",PAGE_SIZE5);
	printf("PgSz: 0x%x\n",PAGE_SIZE);
    printf("PgSft: 0x%x\n",PAGE_SHIFT);
	printf("ValSz: 0x%x\n",VAL_SIZE);
	printf("BufSzB: 0x%x\n",buf_size);
    printf("BufSzL: 0x%x\n",BUF_SIZE_DW);
        
   // Allocate some memory to manipulate
   unsigned long *buffer = (unsigned long*) malloc(buf_size);
   if(buffer == NULL) {
      //fprintf(stderr, "Failed to allocate memory for buffer\n");
      printf("failed to allocate memory for buffer\n");
      exit(1);
   }


   // Lock the page in memory
   // Do this before writing data to the buffer so that any copy-on-write
   // mechanisms will give us our own page locked in memory
   if(mlock(buffer, buf_size) == -1) {
      //fprintf(stderr, "Failed to lock page in memory: %s\n", strerror(errno));
      printf("failed to lock page in memory\n");
      exit(1);
   }

   // Add some data to the memory
   //strncpy(buffer, ORIG_BUFFER, strlen(ORIG_BUFFER));
   for (int i = 0; i < BUF_SIZE_DW;i++){
   	//buffer[i]=0xDEAD;
	buffer[i]= -INIT;
   }
   return buffer;
}

unsigned long get_page_frame_number_of_address(void *addr) {
   // Open the pagemap file for the current process
   FILE *pagemap = fopen("/proc/self/pagemap", "rb");

   // Seek to the page that the buffer is on
   unsigned long offset = (unsigned long)addr / PAGE_SIZE * PAGEMAP_LENGTH;
   if(fseek(pagemap, (unsigned long)offset, SEEK_SET) != 0) {
      // fprintf(stderr, "Failed to seek pagemap to proper location\n");
      printf("failed to seek pagemap to proper location\n");
      exit(1);
   }

   // The page frame number is in bits 0-54 so read the first 7 bytes and clear the 55th bit
   unsigned long page_frame_number = 0;
   fread(&page_frame_number, 1, PAGEMAP_LENGTH-1, pagemap);

   page_frame_number &= 0x7FFFFFFFFFFFFF;

   fclose(pagemap);

   return page_frame_number;
}

unsigned long getPhysAddr(unsigned long pfn, unsigned long addr ) {
  unsigned int offset = addr % PAGE_SIZE;
  unsigned long physical_addr = (pfn << PAGE_SHIFT) + offset;
  return physical_addr;
}

void hammer(unsigned long addra, unsigned long addrb) {

    unsigned long temp = -INIT;
    // hammer using DC CVAC + STR without DSB
    for (int j = 0; j < HAMMER_ROUND; ++j) {
      asm volatile(
        "ldr %2, [%0]\n\t"
        "ldr %2, [%1]\n\t"
        "dc cvac, %0\n\t"
        "dc cvac, %1\n\t"
        //"dsb 0xb"
        ::"r" (addra), "r" (addrb), "r" (temp)
      );
    }

}

int sweep(unsigned long* buf){
    printf("sweepstart\n");
    int count = 0;
    for(int i = 0; i < BUF_SIZE_DW; i++){
        if (buf[i] != -INIT){
            printf("buf:i = 0x%lx",buf[i]);
            count++;
        }
    }
    printf("sweepend count: %d\n",count);
    return count;
}

void rowhammer(unsigned long* buf, int* index) {
    int a_start = 962;
    int a_end = a_start+64;
    int b_start = 1090;
    int b_end = b_start+64;
    
    unsigned long addra;
    unsigned long addrb;
    
    int cnt = 0;
    
    for(int a = a_start; a < a_end; a++){
        printf("a: %d: ",a);
        addra = buf[a];
        printf("addr: 0x%lx\n",addra);
        for (int b = b_start; b < b_end; b++){
            printf("b: %d: ",b);
            addrb = buf[b];
            hammer(addra,addrb);
            printf("addr: 0x%lx\n",addrb);

        }
        cnt+=sweep(buf);
    }
    printf("total count: %d",cnt);
}


int main(){
  unsigned long* buf = createBuffer();
  int i = 0;
  int p = 0;
  int tab_size = PG_COUNT+1;

  unsigned long pg_size[tab_size];
  unsigned long pg_tab[tab_size];
  int pg_index[tab_size];
  for(i = 0; i < tab_size; i++){
    pg_tab[i]=0;
    pg_size[i]=0;
    pg_index[i]=0;
  }

  for(i = 0; i < BUF_SIZE_DW; i++){
    
    unsigned long pfn = get_page_frame_number_of_address((void*)&buf[i]);
    unsigned long addr = (unsigned long) &buf[i];
    printf("buf:%d: 0x%x\n",i,buf[i]);
    printf(" pfn: 0x%lx\n",pfn);
    printf(" addr: 0x%lx\n",&buf[i]);
    printf(" phys_addr: 0x%lx\n", getPhysAddr(pfn,addr));
      
    if (pg_tab[p] != pfn){
      if (i == 0) {
        pg_tab[0]=pfn;
      } else {
        p++;
        
        if (p >= tab_size) {
          printf("p greater than tabsize\n");
        }
          
        pg_tab[p]=pfn;
        pg_index[p]=i;
      }
    }
    pg_size[p]++;
  }

  for(i = 0; i < tab_size; i++){
    printf("pg%d: 0x%x, size: %lu, index: %d\n",i,pg_tab[i],pg_size[i],pg_index[i]);
  }

    
  rowhammer(buf,pg_index);


  free(buf);
}
