#include "IR.h"

#include <stdlib.h>
#include <string.h>

Value* create_value(ValueKind vk, Type* tp) {
  Value* v = (Value*)malloc(sizeof(Value));
  memset(v, 0, sizeof(Value));
  v->vk = vk;
  v->tp = tp;
  return v;
}

void add_use(Value* def, Value* user) {
  if (def == NULL || user == NULL) return;
  Use* u = (Use*)malloc(sizeof(Use));
  u->def = def;
  u->user = user;
  u->nxt = def->use_list;
  u->pre = NULL;
  if (def->use_list != NULL) {
    def->use_list->pre = u;
  }
  def->use_list = u;
}
