#ifndef List_H
#define List_H

typedef struct _List List;

struct _List {
  void * object;
  List * next;
};

List *List_new(void);
List *List_createElement();
void List_delete(List *me);
void List_addObject(List *me, void *newObject);
List *List_prepend(List *me, void *newObject);

#endif /* List_H */

