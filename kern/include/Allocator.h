//
// Created by attil on 10/11/2020.
//
struct bitMap{
    unsigned char *freeRamFrames; //1 se libero 0 se occupato
    int nRamFrames;
    struct spinlock freemem_lock;
    int PAGE_SIZE;
}typedef bitmap;

bitmap * bitmap_init(int ram_size,int PAGE_SIZE); //inizializza la bitmap


static paddr_t
getppages(unsigned long npages); //cerca pagine continue, non usa la ram_stelmem



static int
freepage(int index); //dealloca una pagina

//---------Funzioni chiamate dalla kmalloc-------

vaddr_t
alloc_kpages(unsigned npages)


void
free_kpages(vaddr_t addr,int npages)

//---------------------------------------------