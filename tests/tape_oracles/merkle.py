"""Independent RFC 9162 Merkle tree implementation used as the proof oracle. Uses only hashlib."""
import hashlib
import math

VECTOR_ROOTS = [
    "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d",
    "fac54203e7cc696cf0dfcb42c92a1d9dbaf70ad9e621f4bd8d98662f00e3c125",
    "aeb6bcfe274b70a14fb067a5e5578264db0fa9b51af5e0ba159158f329e06e77",
    "d37ee418976dd95753c1c73862b9398fa2a2cf9b4ff0fdfe8b30cd95209614b7",
    "4e3bbb1f7b478dcfe71fb631631519a3bca12c9aefca1612bfce4c13a86264d4",
    "76e67dadbcdf1e10e1b74ddc608abd2f98dfb16fbce75277b5232a127f2087ef",
    "ddb89be403809e325750d3d263cd78929c2942b7942a34b77e122c9594a74c8c",
    "5dc9da79a70659a9ad559cb701ded9a2ab9d823aad2f4960cfe370eff4604328",
]


def leaf(data):
    return hashlib.sha256(b"\x00" + data).digest()


def node(left, right):
    return hashlib.sha256(b"\x01" + left + right).digest()


def split(n):
    k = 1
    while k * 2 < n:
        k *= 2
    return k


def root(hashes):
    if not hashes:
        return hashlib.sha256(b"").digest()
    if len(hashes) == 1:
        return hashes[0]
    k = split(len(hashes))
    return node(root(hashes[:k]), root(hashes[k:]))


def inclusion(hashes, m):
    if len(hashes) <= 1:
        return []
    k = split(len(hashes))
    if m < k:
        return inclusion(hashes[:k], m) + [root(hashes[k:])]
    return inclusion(hashes[k:], m - k) + [root(hashes[:k])]


def consistency(hashes, m, complete=True):
    n = len(hashes)
    if m == n:
        return [] if complete else [root(hashes)]
    k = split(n)
    if m <= k:
        return consistency(hashes[:k], m, complete) + [root(hashes[k:])]
    return consistency(hashes[k:], m - k, False) + [root(hashes[:k])]


def verify_inclusion(leaf_hash, m, n, proof, expected):
    def rebuild(m, n, proof):
        if n == 1:
            return leaf_hash, proof
        k = split(n)
        if m < k:
            sub, rest = rebuild(m, k, proof[:-1])
            return node(sub, proof[-1]), rest
        sub, rest = rebuild(m - k, n - k, proof[:-1])
        return node(proof[-1], sub), rest
    if m >= n or len(proof) != expected_length(m, n):
        return False
    return rebuild(m, n, proof)[0] == expected


def expected_length(m, n):
    length = 0
    while n > 1:
        k = split(n)
        if m < k:
            n = k
        else:
            m, n = m - k, n - k
        length += 1
    return length


def verify_consistency(m, n, old_root, new_root, proof):
    if m == n:
        return not proof and old_root == new_root
    path = list(proof)
    if m & (m - 1) == 0:
        path.insert(0, old_root)
    fn, sn = m - 1, n - 1
    while fn & 1:
        fn >>= 1
        sn >>= 1
    fr = sr = path[0]
    for c in path[1:]:
        if sn == 0:
            return False
        if fn & 1 or fn == sn:
            fr, sr = node(c, fr), node(c, sr)
            if not fn & 1:
                while fn and not fn & 1:
                    fn >>= 1
                    sn >>= 1
        else:
            sr = node(sr, c)
        fn >>= 1
        sn >>= 1
    return sn == 0 and fr == old_root and sr == new_root


def max_inclusion_hashes(n):
    return math.ceil(math.log2(n))
