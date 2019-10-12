#include <stdio.h>
#include <stdlib.h>
#include "myalloc.h"
#include <pthread.h>

/* change me to 1 for more debugging information
 * change me to 0 for time testing and to clear your mind
 */
#define DEBUG 0

void *__heap = NULL;
node_t *__head = NULL;


pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

header_t *get_header(void *ptr) {
  return (header_t *) (ptr - sizeof(header_t));
}

void print_header(header_t *header) {
  printf("[header_t @ %p | buffer @ %p size: %lu magic: %08lx]\n",
	 header,
	 ((void *) header + sizeof(header_t)),
	 header->size,
	 header->magic);
}

void print_node(node_t *node) {
  printf("[node @ %p | free region @ %p size: %lu next: %p]\n",
	 node,
	 ((void *) node + sizeof(node_t)),
	 node->size,
	 node->next);
}

void print_freelist_from(node_t *node) {
  printf("\nPrinting freelist from %p\n", node);
  while (node != NULL) {
    print_node(node);
    node = node->next;
  }
}

void swapNodes(node_t** head_ref, node_t* currHead,
	       node_t* minAddr, node_t* beforeMinAddr)
{
  // Make minAddr the head
  *head_ref = minAddr;

  // Change prev next to be the current head before the swap
  beforeMinAddr->next = currHead;

  // Swap the current head with the new head (minAddr)
  node_t* temp = minAddr->next;
  minAddr->next = currHead->next;
  currHead->next = temp;
}

node_t* sort_free_list(node_t* head)
{
  if (head == NULL) // list is empty
    return head;

  if (head->next == NULL) // there is only a single node
    return head;

  // smallest address
  node_t* minAddr = head;

  // address before the smallest one
  node_t* beforeMinAddr = NULL;
  node_t* ptr;

  // traverse the list till the last node
  for (ptr = head; ptr->next != NULL; ptr = ptr->next)
  {
    if ((void*) ptr->next < (void*)minAddr)
  	{
  	  minAddr = ptr->next;
  	  beforeMinAddr = ptr;
  	}
  }

  // smallest address isn't at the start of the list
  if (minAddr != head) swapNodes(&head, head, minAddr, beforeMinAddr);

  head->next = sort_free_list(head->next);

  return head;
}


void coalesce_freelist() {
  /* coalesce all neighboring free regions in the free list */

  if (DEBUG) printf("In coalesce freelist...\n");

  pthread_mutex_lock(&lock);
  __head = sort_free_list(__head); // sort the free list
  node_t *target = __head;
  node_t *node = target->next;
  pthread_mutex_unlock(&lock);

  /* traverse the free list, coalescing neighboring regions!
   * some hints:
   * --> it might be easier if you sort the free list first!
   * --> it might require multiple passes over the free list!
   * --> it might be easier if you call some helper functions from here
   * --> see print_free_list_from for basic code for traversing a
   *     linked list!
   */
  while(target != NULL)
  {
    while(node != NULL)
  	{
  	  if((node == ((void*)target + sizeof(header_t) + target->size)))
	    {
	      target->size = target->size + node->size + sizeof(header_t);
	      target->next = node->next;
	    }
  	  node = node->next;
  	}
    target = target->next;
  }
}

void destroy_heap() {
  /* after calling this the heap and free list will be wiped
   * and you can make new allocations and frees on a "blank slate"
   */
  free(__heap);
  __heap = NULL;
  __head = NULL;
}

/* In reality, the kernel or memory allocator sets up the initial heap. But in
 * our memory allocator, we have to allocate our heap manually, using malloc().
 * YOU MUST NOT ADD MALLOC CALLS TO YOUR FINAL PROGRAM!
 */
void init_heap() {
  /* FOR OFFICE USE ONLY */

  if ((__heap = malloc(HEAPSIZE)) == NULL) {
    printf("Couldn't initialize heap!\n");
    exit(1);
  }


  pthread_mutex_lock(&lock);
  __head = (node_t *) __heap;
  __head->size = HEAPSIZE - sizeof(header_t);
  __head->next = NULL;
  pthread_mutex_unlock(&lock);

  if (DEBUG) printf("heap: %p\n", __heap);
  if (DEBUG) print_node(__head);

}

void *first_fit(size_t req_size) {

  void *ptr = NULL; /* pointer to the match that we'll return */

  if (DEBUG) printf("In first_fit with size: %u and freelist @ %p\n", (unsigned) req_size, __head);

  node_t *listitem = __head; /* cursor into our linked list */
  node_t *prev = NULL; /* if listitem is __head, then prev must be null */
  header_t *alloc; /* a pointer to a header you can use for your allocation */

  while(listitem != NULL)
  {
    if (DEBUG) printf("[DEBUG] In loop!\n");
    long unsigned origSize = listitem->size;

    if((listitem->size >= (req_size) && req_size > 0))
  	{
  	  if (DEBUG) printf("listitem->size: %lu\n", listitem->size);
  	  if (DEBUG) printf("req_size: %lu\n", req_size);
  	  if (DEBUG) printf("origSize: %lu\n", origSize);

  	  long unsigned diff = origSize - (req_size) - sizeof(header_t);

  	  node_t *new_node = NULL;
  	  node_t *next = listitem->next;


  	  // there is only one node on the freelist and the node size is not enough space for the allocation with the header
  	  if(prev == NULL && next == NULL && (listitem->size < (req_size + sizeof(header_t))))
	    {
	      return ptr;
	    }

  	  alloc = (void*) listitem;
  	  alloc->magic = HEAPMAGIC;
  	  alloc->size = req_size;

  	  ptr = (void*) alloc + sizeof(header_t);

  	  if((origSize - req_size) >= (sizeof(header_t)))
	    {
	      new_node = (void*) ptr + req_size;
	      new_node->size = diff;
	      new_node->next = next;
	    }
  	  else
      {
  	    // there isn't enough room for the header, so we alloc the entire node to req_size
  	    alloc->size = origSize;
  	  }


  	  if(prev == NULL)  // edge case (listitem is head)
	    {
	      if (DEBUG) printf("[DEBUG] Found 'listitem is head/first free object' edge case!\n");

	      if(new_node != NULL) __head = new_node;
	      else __head = next;

	      break;

	    }
  	  else //else if(next == NULL) // Edge case (listitem is last free object)
	    {
	      if (DEBUG) printf("[DEBUG] Found 'listitem is last free object' edge case!\n");
	      if(new_node != NULL)
    		{
    		  prev->next = new_node; //+ (req_size + sizeof(header_t));
    		  alloc->size = origSize - req_size;
    		  //new_node->next = NULL;
    		}
	      else
    		{
    		  prev->next = next;
    		}
	      break;

	    }

  	  // if (DEBUG) printf("[DEBUG] Found 'end of else if' edge case!\n");
  	  // prev->next = new_node + (req_size + sizeof(header_t));
  	  // new_node->size = diff;
  	  break;

  	}
    prev = listitem;
    prev->size = listitem->size;
    listitem = listitem->next;
  }

  /* traverse the free list from __head! when you encounter a region that
   * is large enough to hold the buffer and required header, use it!
   * If the region is larger than you need, split the buffer into two
   * regions: first, the region that you allocate and second, a new (smaller)
   * free region that goes on the free list in the same spot as the old free
   * list node_t.
   *
   * If you traverse the whole list and can't find space, return a null
   * pointer! :(
   *
   * Hints:
   * --> see print_freelist_from to see how to traverse a linked list
   * --> remember to keep track of the previous free region (prev) so
   *     that, when you divide a free region, you can splice the linked
   *     list together (you'll either use an entire free region, so you
   *     point prev to what used to be next, or you'll create a new
   *     (smaller) free region, which should have the same prev and the next
   *     of the old region.
   * --> If you divide a region, remember to update prev's next pointer!
   */

  if (DEBUG) printf("Returning pointer: %p\n", ptr);
  return ptr;

}
/* myalloc returns a void pointer to size bytes or NULL if it can't.
 * myalloc will check the free regions in the free list, which is pointed to by
 * the pointer __head.
 */

void *myalloc(size_t size) {
  if (DEBUG) printf("\nIn myalloc:\n");
  void *ptr = NULL;

  /* initialize the heap if it hasn't been */
  if (__heap == NULL) {
    if (DEBUG) printf("*** Heap is NULL: Initializing ***\n");
    init_heap();
  }

  /* perform allocation */
  /* search __head for first fit */
  if (DEBUG) printf("Going to do allocation.\n");

  pthread_mutex_lock(&lock);
  ptr = first_fit(size); /* all the work really happens in first_fit */
  pthread_mutex_unlock(&lock);

  if (DEBUG) printf("__head is now @ %p\n", __head);

  return ptr;

}

/* myfree takes in a pointer _that was allocated by myfree_ and deallocates it,
 * returning it to the free list (__head) like free(), myfree() returns
 * nothing.  If a user tries to myfree() a buffer that was already freed, was
 * allocated by malloc(), or basically any other use, the behavior is
 * undefined.
 */
void myfree(void *ptr) {
  if (DEBUG) printf("\nIn myfree with pointer %p\n", ptr);

  header_t *header = get_header(ptr); /* get the start of a header from a pointer */

  if (DEBUG) { print_header(header); }

  if (header->magic != HEAPMAGIC) {
    printf("Header is missing its magic number!!\n");
    printf("It should be '%08lx'\n", HEAPMAGIC);
    printf("But it is '%08lx'\n", header->magic);
    printf("The heap is corrupt!\n");
    return;
  }
  else
  {

    pthread_mutex_lock(&lock);
    int alreadyFree = 0;
    node_t *checkFree = __head;

    // Run through the free list to see if ptr has already been freed
    while(checkFree != NULL) //CHANGE
    {
    	if(checkFree == ptr) alreadyFree = 1;
    	checkFree = checkFree->next;
    }

    // Only free ptr if it hasn't already been freed
    if(alreadyFree == 0)
    {
    	//print_freelist_from(__head);
    	/* free the buffer pointed to by ptr!
    	 * To do this, save the location of the old head (hint, it's __head).
    	 * Then, change the allocation header_t to a node_t. Point __head
    	 * at the new node_t and update the new head's next to point to the
    	 * old head. Voila! You've just turned an allocated buffer into a
    	 * free region!
    	 */

    	node_t *tempPtr = ptr;
    	tempPtr->size = header->size;

    	/* save the current __head of the freelist */
    	node_t *tempHead = __head;
    	tempHead->size = __head->size;

    	/* now set the __head to point to the header_t for the buffer being freed */
    	__head = (void*)header;

    	/* set the new head's next to point to the old head that you saved */
    	__head->next = tempHead;

    	pthread_mutex_unlock(&lock);
    }
  }

}

