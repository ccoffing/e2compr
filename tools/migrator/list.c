#include "list.h"
#include <string.h>
#include <stdlib.h>


List *List_new(void)
{
    List *newList;

    newList = List_createElement();
    newList->object = NULL;
    newList->next = NULL;
    return newList;
}

List *List_createElement()
{
    List *newList;

    newList = (List *) malloc(sizeof(List));
    if (newList == NULL)
      return NULL;

    newList->object = NULL;
    newList->next = NULL;

    return (newList);
}

void List_delete(List *me)
{
    List *head, *tail;

    head = me;
    while (me) {
        tail = me;
        me = me->next;
	if (tail->object)
	  free (tail->object);
	free (tail);
    }
}

void List_add(List *me, void *newObject)
{
    List *newNode = List_createElement();

    newNode->object = newObject;
    newNode->next = me->next;
    me->next = newNode;
}

List *List_prepend(List *me, void *newObject)
{
    List *newNode;

    newNode = List_createElement();
    if (newNode==NULL)
      return me;

    newNode->object = newObject;
    newNode->next = me;
    return newNode;
}
