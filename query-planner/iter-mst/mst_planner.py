"""
    Query Optimizer
    Takes a query of the following format:
    [(v0, (m00, m01)), (v1, (m10, m11), ..., (vn, (mn0, mn1))]
    which is duplicated various times.
    Returns a set of queries that should be made to get the result
"""
import heapq

subq1 = [ (0, (0, 1)), (1, (3, 4)), (2, (1, 3)) ] # vector 0 is on machines 1 and 2, etc...
subq2 = [ (3, (0, 1)), (4, (3, 4)), (7, (1, 3)) ]
subq3 = [ (3, (0, 1)), (4, (3, 4)), (7, (1, 3)) ]
subq4 = [ (3, (0, 1)), (4, (3, 4)), (7, (1, 3)) ]

# TODO more subqs

# count up all the machines to visit
query = [subq1, subq2, subq3, subq4]
needed_machines = set({})

"""
for t in query:
    needed_machines.add(t[1][0])
    needed_machines.add(t[1][1])
"""

num_machines = 10
EXTRA_LOAD = 1

# the default matrix: weight between every vertex is the same by default
# TODO: run a script to determine congestion between each node, save that
# number in a file, and parse the adjacency matrix from it
adjacency_matrix = [[1 if i != j else 0 for i in range(num_machines)] for j in range(num_machines)]

# TODO could also return the load on all the slaves as a point of comparison

def swap(t):
    temp = t[0]
    t[0] = t[1]
    t[1] = temp

#machines = [i for i in range(num_machines)] # XXX: a test example
# You don't need to visit every machine given in the query. First, optimize on the first machine
# given in the tuple, then use the higher-numbered one, and alternate.
def iter_mst(query):
    trees = []
    flipped = False
    global coordinator
    for subq in query:
        vertices = set({})
        vert_vec_map = {}
        for tup in subq:
            # swap
            mt = list(tup[1])
            if (mt[0] > mt[1] and not flipped) or \
                (mt[1] > mt[0] and flipped):
                swap(mt)
            vertices.add(mt[0])
            print("mt = {}".format(mt))
            if mt[0] not in vert_vec_map: vert_vec_map[mt[0]] = [tup[0]]
            else: vert_vec_map[mt[0]].append(tup[0])
        # designate smallest-numbered node we'll visit as the root, or random
        root = list(vertices)[0]
        print("Root = {}".format(root))
        #root = sorted(list(vertices))[0]  #random.sample(vertices, 1)[0]) #XXX: alternatively randomize
        treemap = _mst_prim(vertices, lambda x, y: adjacency_matrix[x][y], \
            root, vert_vec_map)
        # update adjacency matrix
        _recur_update_am(treemap[root])
        flipped = not flipped
        coordinator = root # set the coordinator to be the last chosen root
        trees.append(treemap[root])
    for t in trees:
        print("Printing tree with root {}".format(t.id))
        recur_print(t)
    return (coordinator, trees)

def recur_print(root):
    print(root.id)
    for c in root.children:
        recur_print(c)

def _recur_update_am(root):
    print("Updating, root = {}, vectors = {}".format(root.id, root.vectors))
    for child in root.children:
        adjacency_matrix[root.id][child.id] += EXTRA_LOAD
        adjacency_matrix[child.id][root.id] += EXTRA_LOAD
        _recur_update_am(child)

def _adj(verts, v):
    t = set({})
    for w in range(num_machines): # check every vertex value
        if v != w and w in verts: t.add(w)
    return t

# TODO: make this accessible to the other planners
class Vertex(object):
    def __init__(self, id, key, par=None):
        self.id = id
        self.key = key
        self.par = par
        self.children = []
        self.vectors = [] # TODO: should these be sets or lists

    def __hash__(self):
        return hash(key) ^ hash(par)

def _mst_prim(vertices, w, root, vector_map):
    """
    Perform Prim's algorithm returning the minimum spanning tree of
    the the given vertices with weights determined by the given function,
    from the given root node.
    :param vertices: the set of vertices we're running the algo on
    :w: the weight function
    :param root: the root node (can be chosen arbitrarily)
    """
    prim_verts = {}
    for v in vertices:
        prim_verts[v] = Vertex(v, float("inf"))
    prim_verts[root].key = 0
    queue = list(vertices)
    #queue = list(copy(vertices))
    heapq.heapify(queue) # TODO: replace with Fibonacci heap, for asymptotically optimal performance
    while queue != []:
        u = heapq.heappop(queue)
        for v in _adj(prim_verts, u):
            wgt = w(u, v)
            if wgt < prim_verts[v].key and v in queue:
                prim_verts[v].par = u
                print("Adding {}".format(v))
                prim_verts[u].children.append(prim_verts[v])
                prim_verts[v].key = wgt
    for vert in prim_verts:
        prim_verts[vert].vectors = vector_map[vert]

    return prim_verts

testing = True # TODO switch off for deployment
# Testing
if testing:
    print(iter_mst(query))
    for row in adjacency_matrix: print(row)
