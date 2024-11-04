#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "darray.h"
#include "nand.h"
#include "stack.h"

#define NDEBUG

/**
 * @brief Enum defining types of inputs for NAND gates.
 */
enum input_type {
  EMPTY_INPUT,
  BOOLEAN_SIGNAL_INPUT,
  NAND_GATE_INPUT
};

/**
 * @brief Enum defining the status of NAND gates during topo-sort.
 */
enum topo_sort_status {
  VISITED,              // Gate has been visited.
  NOT_VISITED,          // Gate has not been visited yet.
  CURRENTLY_PROCESSING  // Gate is currently being processed.
};

/**
 * @brief Structure representing state of a node during DFS.
 */
typedef struct {
  nand_t *gate; // Pointer to the gate
  unsigned ind; // Index of input of gate g that needs to
                // be processed next.
} dfs_info_t;

/**
 * @brief Structure representing output information for a gate.
 */
typedef struct {
  nand_t *gate; // Pointer to the gate.
  unsigned ind; // Index of the input of gate g
                // that our gate is connected to.
} out_info_t;

/**
 * @brief Structure representing input information for a gate.
 */
typedef struct {
  void *ptr; // Pointer to an input (boolean signal or a gate).
  enum input_type type; // Type of input.
  unsigned ind; // Index of our gate in the array of outputs
                // of the gate pointed to by ptr.
} in_info_t;

/**
 * @brief Structure representing a NAND gate
 */
typedef struct nand {
  ssize_t crit_path_len; // Length of the critical path.
  in_info_t *inputs; // Array representing inputs of the gate.
  darray_t *outputs; // Dynamic array containing objects of
                     // type out_info_t representing other gates
                     // connected to the output of this gate.
  unsigned input_count; // Number of inputs of this gate.
  enum topo_sort_status status; // Status of the gate during
                                // topological sorting.
  bool signal; // Output signal of this gate.
} nand_t;


void nand_disconnect_nand(nand_t *g_out, nand_t *g_in, unsigned in_ind);
void nand_disconnect_signal(nand_t *g, unsigned k);
void nand_disconnect_input(nand_t *g, unsigned k);
int process_input(in_info_t input, stack_t *stack, stack_t *all_gates);
int process_node(stack_t *stack, stack_t *topo_order, stack_t *all_gates);
int topo_sort_dfs(nand_t *g, stack_t *stack, stack_t *topo_order,
          stack_t *all_gates);
void clear_gates(stack_t *all_gates);
int topo_sort(nand_t **g, unsigned m, stack_t *topo_order);
void evaluate_sorted(stack_t *s);
ssize_t nand_evaluate_all(nand_t **g, bool *s, unsigned m);

/**
 * @brief Disconnects gate g_out from the in_ind-th input of gate g_in.
 * 
 * Helper function that disconnects gate g_out from the in_ind-th input of 
 * gate g_in. 
 * 
 * If either of the pointers is NULL or if g_out is not connected to the 
 * given input of g_in, the behaviour is undefined.
 * 
 * @param g_out Pointer to the output gate.
 * @param g_in Pointer to the input gate.
 * @param in_ind Index of the input of g_in that g_out is connected to.
 */
void nand_disconnect_nand(nand_t *g_out, nand_t *g_in, unsigned in_ind) {
  assert(g_out != NULL && g_in != NULL);
  
  assert(in_ind < g_in->input_count);
  
  unsigned out_ind = g_in->inputs[in_ind].ind;
  unsigned g_out_sz = darray_size(g_out->outputs);
  
  assert(out_ind < g_out_sz);
  
  if (out_ind + 1 != g_out_sz) {
    // Move the element pointing to g_in in the dynamic array representing
    // outputs of g_out to the back of this array by swapping it with the 
    // last element.
    darray_swap_elements(g_out->outputs, out_ind, g_out_sz - 1);
    
    
    // Update information about where connection to out_info->gate is 
    // stored in the dynamic array representing outputs of g_out.
    out_info_t *out_info = darray_get(g_out->outputs, out_ind);
    out_info->gate->inputs[out_info->ind].ind = out_ind;
  }
  
  // Now the last element of g_out->outputs is pointing to g_in, so we can
  // remove it with pop_back in amortized constant time.
  darray_pop_back(g_out->outputs);
  
  g_in->inputs[in_ind].ptr = NULL;
  g_in->inputs[in_ind].type = EMPTY_INPUT;
  g_in->inputs[in_ind].ind = 0;
}

/**
 * @brief Disconnects signal from the k-th input of gate g.
 * 
 * Helper function that disconnects boolean signal from the k-th input 
 * of gate g.
 * 
 * If g is NULL or k is not less than the number of inputs of g, the
 * behaviour is undefined.
 * 
 * @param g Pointer to the gate.
 * @param k Index of the input of g that will be disconnected.
 */
void nand_disconnect_signal(nand_t *g, unsigned k) {
  assert(g != NULL);
  assert(k < g->input_count);
  
  g->inputs[k].ptr = NULL;
  g->inputs[k].type = EMPTY_INPUT;
  g->inputs[k].ind = 0;
}

/**
 * @brief Disconnects the k-th input of gate g.
 * 
 * Helper function that disconnects the k-th input of gate g. Does nothing
 * is the input is already empty.
 * 
 * If g is NULL or k is not less than the number of inputs of g, the
 * behaviour is undefined.
 * 
 * @param g Pointer to the gate.
 * @param k Index of the input of g that will be disconnected.
 */
void nand_disconnect_input(nand_t *g, unsigned k) {
  if (g->inputs[k].type == BOOLEAN_SIGNAL_INPUT)
    nand_disconnect_signal(g, k);
  
  if (g->inputs[k].type == NAND_GATE_INPUT)
    nand_disconnect_nand(g->inputs[k].ptr, g, k);
}

/**
 * @brief Helper function that processes a gate connected to an
 * input of another gate.
 *
 * This function processes a gate that is connected to an input of
 * another gate that appeared during sorting gates topologically.
 *
 * It is a helper function which ensures that the gate passed to it
 * is considered in the topological sorting.
 *
 * If the gate has already been visited, the function does nothing.
 *
 * If the gate is still in the DFS recursion stack, it means that
 * there is a cycle in the network. In that case the function returns
 * -1 and sets errno to ECANCELED.
 *
 * If the gate has not been visited yet, it is added to the DFS stack
 * and all_gates stack.
 *
 * In case of a memory allocation failure the function changes errno
 * to ENOMEM and returns -1.
 *
 * In case an invalid argument is passed to the function, its behaviour
 * is undefined.
 *
 * @param g Gate to be processed.
 * @param stack Stack used for DFS traversal.
 * @param all_gates Stack used to store all nodes visited by the DFS.
 * @return 0 on success and -1 on failure.
 */
int process_input_nand(nand_t *g, stack_t *stack, stack_t *all_gates) {
  if (g->status == CURRENTLY_PROCESSING) {
    // There is a cycle in the network.
    errno = ECANCELED;
    return -1;
  }

  if (g->status == NOT_VISITED) {
    // Add h to the stack to be visited by DFS.
    if (stack_push(all_gates, &g) != 0) {
      errno = ENOMEM;
      return -1; // Memory allocation failure.
    }

    g->status = CURRENTLY_PROCESSING;
    dfs_info_t dfs_info = { .gate = g, .ind = 0 };
    if (stack_push(stack, &dfs_info) != 0) {
      errno = ENOMEM;
      return -1; // Memory allocation failure.
    }
  }

  return 0;
}

/**
 * @brief Helper function that processes one of the inputs of a gate.
 *
 * This function takes one of the inputs of a gate and performs all
 * checks necessary to evaluate signal at the output of the gate.
 *
 * It checks whether this input is not-empty. If there is a gate
 * connected to the input it checks whether its output has been
 * already evaluated and if not, it pushes this gate onto the stack.
 *
 * The function changes errno to ECANCELED and returns -1 in case of
 * a failure to evaluate the gate's output signal.
 *
 * In case of a memory allocation failure the function changes errno
 * to ENOMEM and returns -1.
 *
 * In case an invalid argument is passed to the function, its behaviour
 * is undefined.
 *
 * @param input Object representing an input of a gate.
 * @param stack Stack used for DFS traversal.
 * @param all_gates Stack used to store all nodes visited by the DFS.
 * @return 0 on success and -1 on failure.
 */
int process_input(in_info_t input, stack_t *stack, stack_t *all_gates) {
  
  if (input.type == EMPTY_INPUT) {
    // If one of the inputs is empty, then the output of the gate 
    // is undefined and we cannot evaluate the outputs of the gates.
    errno = ECANCELED;
    return -1;
  }
  
  // If there is a gate connected to the input, is has to
  // be processed. Otherwise, there is nothing to be done.
  if (input.type == NAND_GATE_INPUT) {
    if (process_input_nand(input.ptr, stack, all_gates) != 0) {
      return -1;
    }
  }
  
  return 0;
}


/**
 * @brief Helper function processing the top node of a stack in DFS.
 *
 * This function procesesss a node at the top of a DFS stack
 * performed to obtain topological sorting.
 *
 * If an invalid argument is passed to the function, its behaviour
 * is undefined.
 *
 * In case of an error this function sets errno to a corresponding
 * value and returns -1. Variable errno is set to ENOMEM in case of
 * a memory allocation failure or ECANCELED in case the output of
 * a gate cannot be evaluated.
 *
 * @param stack DFS stack.
 * @param topo_order Stack where gates are stored in topological order.
 * @param all_gates Stack used to store all nodes visited by the DFS.
 * @return 0 on success and -1 on failure.
 */
int process_node(stack_t *stack, stack_t *topo_order, stack_t *all_gates) {
  dfs_info_t dfs_info = *((dfs_info_t*)stack_top(stack));
  stack_pop(stack);
  
  nand_t *g = dfs_info.gate;
  unsigned k = dfs_info.ind;
  
  if (k == g->input_count) {
    // All inputs of g have been evaluated, so we can put it
    // into the topological order.
    g->status = VISITED;
    if (stack_push(topo_order, &g) != 0) {
      errno = ENOMEM;
      return -1;
    }
    return 0;
  }
  
  dfs_info.ind++;
  // Put g back into the stack to evaluate its next input.
  if (stack_push(stack, &dfs_info) != 0) {
    errno = ENOMEM;
    return -1;
  }
  
  return process_input(g->inputs[k], stack, all_gates);
}

/**
 * @brief Helper function for topo-sort. Performs DFS starting at a given gate.
 *
 * This function performs depth-first search (DFS) algorithm starting
 * from a given gate and sorts the visited gates topologically.
 *
 * It pushes nodes visited by DFS onto topo_order stack in their topological
 * order. It also pushes all nodes visited by it onto the stack all_gates
 * in arbitrary order. In case of an error all_gates is guaranteed to contain
 * all gates visited by DFS to the point of the error, but topo_order is not.
 *
 * If an invalid argument is passed to the function, its behaviour
 * is undefined.
 *
 * In case of an error this function sets errno to a corresponding
 * value and returns -1. Variable errno is set to ENOMEM in case of
 * a memory allocation failure or ECANCELED in case the output of
 * a gate cannot be evaluated.
 *
 * @param g Starting point for the DFS algorithm.
 * @param stack DFS stack.
 * @param topo_order Stack where gates are stored in topological order.
 * @param all_gates Stack used to store all nodes visited by the DFS.
 * @return 0 on success and -1 on failure.
 */
int topo_sort_dfs(nand_t *g, stack_t *stack, stack_t *topo_order,
          stack_t *all_gates) {
  dfs_info_t dfs_info = { .gate = g, .ind = 0 };
  
  // Initiate the stack with the starting gate.
  if (stack_push(all_gates, &g) != 0 || 
      stack_push(stack, &dfs_info) != 0) {
    errno = ENOMEM;
    return -1;
  }
  g->status = CURRENTLY_PROCESSING;
  

  while (stack_size(stack) > 0) {
    if (process_node(stack, topo_order, all_gates) != 0) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Helper function marking gates as unvisited after DFS traversal.
 *
 * The function removes all elements of the stack passed to it.
 *
 * If any of the argument passed is invalid, its behaviour is undefined.
 *
 * @param all_gates Stack containing gates that will be marked as unvisited.
 */
void clear_gates(stack_t *all_gates) {
  while (stack_size(all_gates) > 0) {
    nand_t *h = *(nand_t**)stack_top(all_gates);
    stack_pop(all_gates);
    
    h->status = NOT_VISITED;
  }
}

/**
 * @brief Helper function performing topo-sort of gates.
 *
 * This function sorts the part of the network induced by gates in the
 * array g. It pushes the nodes onto the topo_order stack according
 * to their topological ordering.
 *
 * In case a cycle is detected or a memory allocation error occurs,
 * the function returns -1 and the content of the topo_order stack
 * is undefined. In that case, the function sets errno to ECANCELED
 * or ENOMEM respectively.
 *
 * If any of the arguments passed is invalid, behaviour is undefined.
 *
 * @param g Array of gates inducing the part of the network to be sorted.
 * @param m Size of the array g.
 * @param topo_order Stack to put gates according to their topo-ordering.
 * @return 0 on success and -1 on failure.
 */
int topo_sort(nand_t **g, unsigned m, stack_t *topo_order) {
  stack_t *stack = stack_new(sizeof(dfs_info_t));
  
  if (stack == NULL) {
    errno = ENOMEM;
    return -1;
  }
  
  stack_t *all_gates = stack_new(sizeof(nand_t *));
  
  if (all_gates == NULL) {
    errno = ENOMEM;
    stack_delete(stack);
    return -1;
  }
  
  int ret = 0; // Return value - zero on success and non-zero on failure.
  for (unsigned i = 0; i < m && ret == 0; i++) {
    if (g[i]->status == VISITED)
      continue;
    
    if (topo_sort_dfs(g[i], stack, topo_order, all_gates) != 0)
      ret = -1;
  }
  
  clear_gates(all_gates);
  stack_delete(stack);
  stack_delete(all_gates);
  
  stack_reverse(topo_order);
  return ret;
}

/**
 * @brief Helper function that evaluates gates given their topo-sorting.
 *
 * This function takes a stack containing gates in a topologically
 * sorted order and evaluates their outputs.
 *
 * It also computes the length of the critical path for all the gates.
 *
 * If the argument passed is invalid, the behaviour is undefined.
 *
 * @param s Stack containing gates in a topologically sorted order.
 */
void evaluate_sorted(stack_t *s) {
  while (stack_size(s) > 0) {
    nand_t *g = *((nand_t**)stack_top(s));
    stack_pop(s);
    
    g->signal = true;
    g->crit_path_len = 0;
    for (unsigned i = 0; i < g->input_count; i++) {
      assert(g->inputs[i].type != EMPTY_INPUT);
      
      if (g->inputs[i].type == BOOLEAN_SIGNAL_INPUT) {
        if (g->crit_path_len < 1)
          g->crit_path_len = 1;
        g->signal &= *((bool*)g->inputs[i].ptr);
        
      }
      else {
        
        nand_t *h = (nand_t*)g->inputs[i].ptr;
        if (g->crit_path_len < h->crit_path_len + 1)
          g->crit_path_len = h->crit_path_len + 1;
          
        g->signal &= h->signal;
      }
    }
    
    // Variable g->signal contains AND of inputs of g,
    // so we need to negate it to obtain NAND.
    g->signal = !g->signal;
  }
}

/**
 * @brief Helper function for evaluating gates' outputs.
 *
 * This function takes an array of gates and attempts to evaluate
 * their outputs as well as compute the length of the critical path.
 *
 * In case of an error in the network (there is a cycle or some gate
 * has an empty input), the function returns -1 and sets errno to
 * ECANCELED.
 *
 * In case of a memory allocation error, the function returns -1 and
 * sets errno to ENOMEM.
 *
 * If the argument passed are invalid, the behaviour is undefined.
 *
 * @param g Array of gates that should be evaluated.
 * @param s Array of booleans where the result should be stored.
 * @param m The size of the arrays g and s.
 * @return Length of the critical path or -1 in case of an error.
 */
ssize_t nand_evaluate_all(nand_t **g, bool *s, unsigned m) {
  stack_t *topo_order =  stack_new(sizeof(nand_t *));
  
  ssize_t l = 0; // Length of the critical path or -1
           //  in case of an error.
  if (topo_order == NULL) {
    errno = ENOMEM;
    l = -1; // Memory allocation failure.
  }
  
  if (l != -1 && topo_sort(g, m, topo_order) != 0)
    l = -1; // Topo sort failure.
    
  if (l != -1) {
    evaluate_sorted(topo_order);
    for (unsigned i = 0; i < m; i++) {
      s[i] = g[i]->signal;
      if (l < g[i]->crit_path_len)
        l = g[i]->crit_path_len;
    }
  }
  
  stack_delete(topo_order);
  return l;
}

nand_t* nand_new(unsigned n) {
  nand_t *g = (nand_t*)malloc(sizeof(nand_t));
  
  if (g == NULL) {
    errno = ENOMEM;
    return NULL; // Memory allocation failure.
  }
  
  g->outputs = darray_new(sizeof(out_info_t));
  g->inputs = (in_info_t*)malloc(sizeof(in_info_t) * n);
  
  if (g->outputs== NULL || g->inputs == NULL) {
    darray_delete(g->outputs);
    free(g->inputs);
    free(g);
    errno = ENOMEM;
    return NULL; // Memory allocation failure.
  }
  
  g->input_count = n;
  g->status = NOT_VISITED;
  g->signal = false;
  g->crit_path_len = 0;
  
  for (unsigned i = 0; i < n; i++) {
    g->inputs[i].ptr = NULL;
    g->inputs[i].type = EMPTY_INPUT;
    g->inputs[i].ind = 0;
  }
  
  return g;
}

void nand_delete(nand_t *g) {
  if (g == NULL)
    return;
  
  // Disconnect all inputs of g.
  for (unsigned i = 0; i < g->input_count; i++)
    if (g->inputs[i].type != EMPTY_INPUT)
      nand_disconnect_input(g, i);
  
  // Disconnect all outputs of g.
  while (darray_size(g->outputs) > 0) {
    out_info_t *info = (out_info_t*)darray_back(g->outputs);
    nand_disconnect_nand(g, info->gate, info->ind);
  }
  
  darray_delete(g->outputs);
  free(g->inputs);
  free(g);
}

int nand_connect_nand(nand_t *g_out, nand_t *g_in, unsigned k) {
  if (g_out == NULL || g_in == NULL || k >= g_in->input_count) {
    errno = EINVAL;
    return -1;
  }
  
  if (g_in->inputs[k].ptr == g_out)
    return 0; // Do nothing if the gates are already connected.
  

  // Push the gate g_in into the dynamic array containing
  // outputs of gate g_out.
  out_info_t out_info = { .gate = g_in, .ind = k };
  if (darray_push_back(g_out->outputs, &out_info) == -1) {
    errno = ENOMEM;
    return -1;
  }
  
  // Disconnect k-th output of the gate g_in.
  // Note that if a gate is connected to this input it has
  // to be different from g_out and therefore g_out->outputs
  // is not invalidated.
  nand_disconnect_input(g_in, k);

  g_in->inputs[k].ptr = (void*)g_out;
  g_in->inputs[k].type = NAND_GATE_INPUT;
  g_in->inputs[k].ind = darray_size(g_out->outputs) - 1;
  
  return 0;
}

int nand_connect_signal(bool const *s, nand_t *g, unsigned k) {
  if (s == NULL || g == NULL || k >= g->input_count) {
    errno = EINVAL;
    return -1;
  }
  
  nand_disconnect_input(g, k);
  
  g->inputs[k].ptr = (void*)s;
  g->inputs[k].type = BOOLEAN_SIGNAL_INPUT;
  g->inputs[k].ind = 0;
  
  return 0;
}

ssize_t nand_evaluate(nand_t **g, bool *s, size_t m) {
  if (g == NULL || s == NULL || m == 0) {
    errno = EINVAL;
    return -1;
  }
  
  for (size_t i = 0; i < m; i++) {
    if (g[i] == NULL) {
      errno = EINVAL;
      return -1;
    }
  }
  
  return nand_evaluate_all(g, s, m);
}

ssize_t nand_fan_out(nand_t const *g) {
  if (g == NULL) {
    errno = EINVAL;
    return -1;
  }
  return darray_size(g->outputs);
}

void *nand_input(nand_t const *g, unsigned k) {
  if (g == NULL || k >= g->input_count) {
    errno = EINVAL;
    return NULL;
  }
  
  if (g->inputs[k].type == EMPTY_INPUT) {
    errno = 0;
    return NULL;
  }
  
  return g->inputs[k].ptr;
}

nand_t* nand_output(nand_t const *g, ssize_t k) {
  if (g == NULL || k >= nand_fan_out(g)) {
    errno = EINVAL;
    return NULL;
  }
  return ((out_info_t*)darray_get(g->outputs, k))->gate;
}

#ifdef NDEBUG
#undef NDEBUG
#endif
