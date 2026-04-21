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

void add_use(Value* def, Value* user, int op_index) {
  if (def == NULL || user == NULL) return;
  Use* u = (Use*)malloc(sizeof(Use));
  u->def = def;
  u->user = user;
  u->nxt = def->use_list;
  u->op_index = op_index;
  u->pre = NULL;
  if (def->use_list != NULL) {
    def->use_list->pre = u;
  }
  def->use_list = u;
}

// 从 def 的 use_list 中，移除特定的 user
void remove_use(Value* def, Value* user) {
  if (def == NULL || def->use_list == NULL) return;
  Use* cur = def->use_list;
  while (cur != NULL) {
    if (cur->user == user) {
      if (cur->pre)
        cur->pre->nxt = cur->nxt;
      else
        def->use_list = cur->nxt;

      if (cur->nxt) cur->nxt->pre = cur->pre;
      free(cur);
      return;
    }
    cur = cur->nxt;
  }
}