#!/usr/bin/env python3
"""AFTERIMAGE — Intelligent Local Memory Retrieval Server.

Bridges conversational human memory queries, temporal reasoning, concept expansion,
and SQLite inspection to the AFTERIMAGE C++ engine.
"""

from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
import datetime
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import sqlite3
import sys
import threading
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'web'
ENGINE = ROOT / 'build' / 'afterimage'
SPOTLIGHT_BRIDGE = ROOT / 'build' / 'afterimage-spotlight'


def get_database_path():
    env_db = os.environ.get('AFTERIMAGE_DB')
    if env_db:
        return Path(env_db).expanduser().resolve()
    # Check if data/afterimage.db exists in project root
    workspace_db = ROOT / 'data' / 'afterimage.db'
    if workspace_db.exists():
        return workspace_db
    # Try ~/.afterimage/library.db
    home_db = Path.home() / '.afterimage' / 'library.db'
    try:
        home_db.parent.mkdir(parents=True, exist_ok=True)
        test_file = home_db.parent / '.test_write'
        test_file.touch()
        test_file.unlink()
        return home_db
    except Exception:
        workspace_db.parent.mkdir(parents=True, exist_ok=True)
        return workspace_db


DB = get_database_path()


# ---------------------------------------------------------------------------
# Database schema upgrade & helpers
# ---------------------------------------------------------------------------

def get_db_connection():
    db_path = get_database_path()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path), check_same_thread=False)
    conn.row_factory = sqlite3.Row
    ensure_schema(conn)
    return conn


def ensure_schema(conn):
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS documents (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            file_path TEXT NOT NULL UNIQUE,
            file_type TEXT NOT NULL,
            extracted_text TEXT NOT NULL DEFAULT '',
            size_bytes INTEGER,
            modified_at_epoch_seconds INTEGER
        )
    """)
    cur.execute("PRAGMA table_info(documents)")
    existing_cols = {row[1] for row in cur.fetchall()}
    if 'tags' not in existing_cols:
        try: cur.execute("ALTER TABLE documents ADD COLUMN tags TEXT DEFAULT ''")
        except Exception: pass
    if 'user_notes' not in existing_cols:
        try: cur.execute("ALTER TABLE documents ADD COLUMN user_notes TEXT DEFAULT ''")
        except Exception: pass
    if 'extraction_method' not in existing_cols:
        try: cur.execute("ALTER TABLE documents ADD COLUMN extraction_method TEXT DEFAULT ''")
        except Exception: pass
    if 'page_count' not in existing_cols:
        try: cur.execute("ALTER TABLE documents ADD COLUMN page_count INTEGER DEFAULT 1")
        except Exception: pass
    if 'indexed_at_epoch_seconds' not in existing_cols:
        try: cur.execute("ALTER TABLE documents ADD COLUMN indexed_at_epoch_seconds INTEGER DEFAULT 0")
        except Exception: pass
    cur.execute("""
        CREATE TABLE IF NOT EXISTS semantic_vectors (
            document_id INTEGER PRIMARY KEY REFERENCES documents(id) ON DELETE CASCADE,
            content_hash TEXT NOT NULL,
            dimensions INTEGER NOT NULL,
            vector BLOB NOT NULL,
            indexed_at_epoch_seconds INTEGER NOT NULL
        )
    """)
    conn.commit()


# ---------------------------------------------------------------------------
# Query Understanding & Human Memory Recollection Parser
# ---------------------------------------------------------------------------

COMMON_TYPOS = {
    'assignmnet': 'assignment',
    'asignment': 'assignment',
    'assignmnt': 'assignment',
    'schedulng': 'scheduling',
    'schedulin': 'scheduling',
    'algoritm': 'algorithm',
    'algoritms': 'algorithms',
    'databse': 'database',
    'databses': 'databases',
    'proccess': 'process',
    'proccesses': 'processes',
    'interupt': 'interrupt',
    'interupts': 'interrupts',
    'synch': 'sync',
    'hierachy': 'hierarchy',
    'inheritence': 'inheritance',
    'polymorphsm': 'polymorphism',
    'encapsulaton': 'encapsulation',
    'distributd': 'distributed',
    'concurancy': 'concurrency',
    'transction': 'transaction',
    'transctions': 'transactions',
    'structur': 'structure',
    'reponsitory': 'repository',
    'documnt': 'document',
    'artical': 'article',
    'archtecture': 'architecture',
    'optmization': 'optimization',
    'paralel': 'parallel',
    'pading': 'paging',
    'consenss': 'consensus',
    'consensous': 'consensus',
}

# Domain knowledge graph & concept taxonomy
CONCEPT_TAXONOMY = {
    'oops': ['object', 'class', 'classes', 'inheritance', 'polymorphism', 'encapsulation', 'abstraction', 'methods', 'constructor', 'interface'],
    'oop': ['object', 'class', 'classes', 'inheritance', 'polymorphism', 'encapsulation', 'abstraction', 'methods', 'constructor'],
    'java': ['java', 'class', 'classes', 'object', 'objects', 'jvm', 'jdk', 'bytecode', 'interface', 'package', 'streams', 'multithreading'],
    'assignment': ['assignment', 'lab', 'homework', 'project', 'exercise', 'submission', 'practical', 'task'],
    'os': ['operating system', 'kernel', 'process', 'virtual memory', 'paging', 'threads', 'scheduling', 'deadlock', 'filesystem'],
    'operating system': ['kernel', 'process', 'virtual memory', 'paging', 'threads', 'scheduling', 'deadlock', 'processes'],
    'dbms': ['database', 'indexing', 'b-tree', 'sql', 'transactions', 'acid', 'relational', 'normalization', 'normal form', 'queries', 'joins'],
    'database': ['indexing', 'b-tree', 'sql', 'transactions', 'acid', 'relational', 'normalization', 'normal form', 'tables', 'duplicate'],
    'dsa': ['data structures', 'algorithms', 'binary search tree', 'tree', 'graph', 'sorting', 'heap', 'hash', 'stack', 'queue'],
    'data structures': ['algorithms', 'binary search tree', 'tree', 'graph', 'sorting', 'heap', 'hash table', 'linked list', 'adsa'],
    'network': ['tcp', 'ip', 'udp', 'socket', 'http', 'dns', 'packet', 'routing', 'osi', 'communication'],
    'networks': ['tcp', 'ip', 'udp', 'socket', 'http', 'dns', 'packet', 'routing', 'osi', 'communication'],
    'distributed': ['consensus', 'raft', 'paxos', 'replication', 'distributed transactions', 'leader election'],
    'ml': ['machine learning', 'neural network', 'transformer', 'attention', 'embeddings', 'model', 'weights', 'deep learning'],
    'ai': ['machine learning', 'neural', 'transformer', 'attention', 'model', 'deep learning'],
    'concurrency': ['threads', 'mutex', 'lock', 'async', 'semaphore', 'atomic', 'race condition', 'synchronization'],
    'compiler': ['optimization', 'lexer', 'parser', 'ast', 'intermediate representation', 'register', 'code generation'],

    # Added domain concept taxonomies for comprehensive memory recall
    'cpu': ['cpu', 'processor', 'registers', 'instruction', 'alu', 'architecture', 'pipeline', 'risc', 'cisc', 'program counter'],
    'instruction': ['instruction', 'instructions', 'fetch', 'decode', 'execute', 'program counter', 'opcode', 'cycle', 'machine instructions'],
    'instructions': ['instruction', 'instructions', 'fetch', 'decode', 'execute', 'program counter', 'opcode', 'machine instructions'],
    'architecture': ['architecture', 'architectures', 'risc', 'cisc', 'cpu', 'isa', 'instruction set', 'pipeline'],
    'architectures': ['architecture', 'architectures', 'risc', 'cisc', 'cpu', 'isa', 'instruction set'],
    'ram': ['ram', 'memory', 'volatile', 'random access memory', 'sram', 'dram', 'main memory'],
    'rom': ['rom', 'read only memory', 'non-volatile', 'bios', 'firmware'],
    'cache': ['cache', 'caching', 'l1', 'l2', 'l3', 'locality', 'cache line', 'cache hit', 'frequently used', 'temporarily store'],
    'caching': ['cache', 'caching', 'l1', 'l2', 'l3', 'locality', 'temporarily store', 'frequently used'],
    'deadlock': ['deadlock', 'deadlocks', 'coffman', 'circular wait', 'mutual exclusion', 'hold and wait', 'banker'],
    'deadlocks': ['deadlock', 'deadlocks', 'coffman', 'circular wait', 'mutual exclusion', 'banker'],
    'process': ['process', 'pcb', 'process control block', 'scheduling', 'context switch', 'processes'],
    'processes': ['process', 'pcb', 'process control block', 'scheduling', 'context switch', 'processes'],
    'multithreading': ['thread', 'threads', 'multithreading', 'concurrency', 'synchronization', 'mutex', 'lock', 'shared resources'],
    'thread': ['thread', 'threads', 'multithreading', 'concurrency', 'synchronization', 'mutex', 'lock'],
    'threads': ['thread', 'threads', 'multithreading', 'concurrency', 'synchronization', 'mutex', 'shared resources'],
    'normalization': ['normalization', 'normal form', '1nf', '2nf', '3nf', 'bcnf', 'redundancy', 'anomaly', 'functional dependency'],
    'duplicate': ['duplicate', 'unique', 'primary key', 'redundancy', 'constraint', 'candidate key', 'prevents duplicate'],
    'joins': ['join', 'joins', 'inner join', 'left join', 'outer join', 'sql join', 'cross join'],
    'join': ['join', 'joins', 'inner join', 'left join', 'outer join', 'sql join'],
    'tables': ['table', 'tables', 'relation', 'foreign key', 'schema', 'relational database', 'relationships'],
    'binary search': ['binary search', 'binary search tree', 'bst', 'search tree', 'logarithmic'],
    'tree': ['tree', 'binary search tree', 'bst', 'b-tree', 'traversal', 'nodes', 'tree structure'],
    'trees': ['tree', 'binary search tree', 'bst', 'b-tree', 'traversal', 'nodes', 'tree structure'],
    'sorting': ['sorting', 'quicksort', 'mergesort', 'heapsort', 'sort', 'algorithm', 'sorting algorithms'],
    'inheritance': ['inheritance', 'subclass', 'superclass', 'extends', 'polymorphism', 'derived', 'base class'],
    'classes': ['class', 'classes', 'object', 'objects', 'instance', 'constructor', 'methods'],
    'objects': ['class', 'classes', 'object', 'objects', 'instance', 'instantiation'],
    'stream': ['stream', 'streams', 'filter', 'map', 'reduce', 'collector', 'java streams'],
    'streams': ['stream', 'streams', 'filter', 'map', 'reduce', 'collector', 'java streams'],
    'exception': ['exception', 'exceptions', 'try', 'catch', 'finally', 'throw', 'error handling', 'runtime errors'],
    'exceptions': ['exception', 'exceptions', 'try', 'catch', 'finally', 'throw', 'error handling', 'runtime errors'],
    'serialization': ['serialization', 'deserialization', 'serializable', 'byte stream', 'object stream'],
    'deserialization': ['serialization', 'deserialization', 'serializable', 'byte stream'],
    'probability': ['probability', 'distribution', 'distributions', 'random variable', 'stats', 'statistics'],
    'distributions': ['distribution', 'distributions', 'probability', 'gaussian', 'normal distribution', 'poisson'],
    'deviation': ['standard deviation', 'variance', 'sigma', 'mean', 'dispersion', 'formula'],
    'correlation': ['correlation', 'pearson', 'covariance', 'regression', 'formula', 'statistics'],
    'hypothesis': ['hypothesis', 'null hypothesis', 'p-value', 't-test', 'significance', 'hypothesis testing'],
    'authentication': ['authentication', 'identity', 'authn', 'password', 'credentials', 'mfa', 'who you are'],
    'authorization': ['authorization', 'permissions', 'authz', 'rbac', 'access control', 'roles', 'what you can do'],
    'osi': ['osi', 'osi model', '7 layers', 'transport', 'network layer', 'data link', 'packet', 'encapsulation'],
    'devices': ['devices', 'external devices', 'i/o', 'interrupts', 'device drivers', 'dma', 'peripherals'],
    'communication': ['communication', 'data communication', 'network', 'packets', 'transmission', 'protocols'],
}

# Small, transparent language bridges. These add recall candidates but never
# replace the words the user actually typed in the primary query.
QUERY_SYNONYMS = {
    'resume': ['cv'], 'cv': ['resume'], 'photo': ['image', 'picture'],
    'picture': ['image', 'photo'], 'screenshot': ['screen capture', 'image'],
    'homework': ['assignment'], 'coursework': ['assignment'],
    'slides': ['presentation'], 'deck': ['presentation', 'slides'],
    'movie': ['video'], 'film': ['video'], 'directory': ['folder'],
    'folder': ['directory'], 'trash': ['deleted', 'recycle bin'],
}


# Local AI semantic retrieval ------------------------------------------------
#
# Ollama serves embeddings on the user's machine. The SQLite table stores only
# float vectors and a content hash; extracted text remains in `documents` and
# is never copied into this semantic index.
OLLAMA_URL = os.environ.get('AFTERIMAGE_OLLAMA_URL', 'http://127.0.0.1:11434').rstrip('/')
EMBEDDING_MODEL = os.environ.get('AFTERIMAGE_EMBEDDING_MODEL', 'nomic-embed-text')
SEMANTIC_TEXT_LIMIT = 12000


def _ollama_embed(inputs):
    payload = json.dumps({'model': EMBEDDING_MODEL, 'input': inputs}).encode('utf-8')
    request = urllib.request.Request(
        OLLAMA_URL + '/api/embed', data=payload,
        headers={'Content-Type': 'application/json'}, method='POST'
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        result = json.loads(response.read().decode('utf-8'))
    vectors = result.get('embeddings')
    if not isinstance(vectors, list) or len(vectors) != len(inputs):
        raise RuntimeError(f'Ollama did not return embeddings for {EMBEDDING_MODEL}.')
    return vectors


def _semantic_text(row):
    labels = ' '.join(filter(None, [row.get('title', ''), row.get('tags', ''), row.get('user_notes', '')]))
    body = (row.get('extracted_text') or '')[:SEMANTIC_TEXT_LIMIT]
    return f'Title: {labels}\nContent: {body}'.strip()


def _vector_blob(vector):
    return struct.pack('<' + 'f' * len(vector), *[float(value) for value in vector])


def _cosine_from_blob(blob, dimensions, query_vector):
    if dimensions != len(query_vector) or len(blob) != dimensions * 4:
        return None
    values = struct.unpack('<' + 'f' * dimensions, blob)
    dot = sum(left * right for left, right in zip(values, query_vector))
    stored_norm = math.sqrt(sum(value * value for value in values))
    query_norm = math.sqrt(sum(value * value for value in query_vector))
    return dot / (stored_norm * query_norm) if stored_norm and query_norm else None


class SemanticIndexer:
    def __init__(self):
        self.lock = threading.Lock()
        self.status = 'idle'
        self.processed = 0
        self.total = 0
        self.error = ''
        self.updated = 0

    def start(self):
        with self.lock:
            if self.status == 'indexing':
                return False, 'A local AI index job is already running.'
            self.status, self.processed, self.total, self.updated, self.error = 'indexing', 0, 0, 0, ''
        threading.Thread(target=self._run, daemon=True).start()
        return True, f'Building semantic vectors with local model {EMBEDDING_MODEL}.'

    def _run(self):
        try:
            conn = get_db_connection()
            cur = conn.cursor()
            cur.execute("SELECT id,title,tags,user_notes,extracted_text FROM documents ORDER BY id")
            documents = [dict(row) for row in cur.fetchall()]
            with self.lock:
                self.total = len(documents)

            pending = []
            for document in documents:
                text = _semantic_text(document)
                digest = hashlib.sha256(text.encode('utf-8')).hexdigest()
                existing = cur.execute('SELECT content_hash FROM semantic_vectors WHERE document_id = ?', (document['id'],)).fetchone()
                if not existing or existing[0] != digest:
                    pending.append((document['id'], digest, text))
                with self.lock:
                    self.processed += 1

            for start in range(0, len(pending), 8):
                batch = pending[start:start + 8]
                vectors = _ollama_embed([entry[2] for entry in batch])
                for (doc_id, digest, _), vector in zip(batch, vectors):
                    cur.execute("""
                        INSERT INTO semantic_vectors(document_id,content_hash,dimensions,vector,indexed_at_epoch_seconds)
                        VALUES(?,?,?,?,?)
                        ON CONFLICT(document_id) DO UPDATE SET content_hash=excluded.content_hash,
                          dimensions=excluded.dimensions, vector=excluded.vector,
                          indexed_at_epoch_seconds=excluded.indexed_at_epoch_seconds
                    """, (doc_id, digest, len(vector), _vector_blob(vector), int(time.time())))
                    with self.lock:
                        self.updated += 1
                conn.commit()
            cur.execute('DELETE FROM semantic_vectors WHERE document_id NOT IN (SELECT id FROM documents)')
            conn.commit()
            conn.close()
            with self.lock:
                self.status = 'completed'
        except Exception as exc:
            with self.lock:
                self.status, self.error = 'error', str(exc)
            print(f'[AFTERIMAGE AI] Semantic index failed: {exc}', file=sys.stderr)

    def snapshot(self):
        with self.lock:
            return {'status': self.status, 'processed': self.processed, 'total': self.total,
                    'updated': self.updated, 'error': self.error, 'model': EMBEDDING_MODEL}


semantic_indexer = SemanticIndexer()


def semantic_candidates(query, limit=60):
    """Return semantic document IDs and cosine scores, or no candidates if local AI is unavailable."""
    conn = get_db_connection()
    rows = conn.execute('SELECT document_id,dimensions,vector FROM semantic_vectors').fetchall()
    conn.close()
    if not rows:
        return {}
    try:
        query_vector = _ollama_embed([query])[0]
    except Exception:
        return {}
    scores = {}
    for row in rows:
        score = _cosine_from_blob(row['vector'], row['dimensions'], query_vector)
        if score is not None:
            scores[row['document_id']] = score
    return dict(sorted(scores.items(), key=lambda item: item[1], reverse=True)[:limit])


def sync_spotlight_index():
    if sys.platform != 'darwin':
        raise RuntimeError('Spotlight integration is available only on macOS.')
    if not SPOTLIGHT_BRIDGE.exists():
        raise RuntimeError('The Spotlight bridge has not been built. Run the macOS build target first.')
    completed = subprocess.run(
        [str(SPOTLIGHT_BRIDGE), str(DB)], cwd=ROOT, text=True,
        capture_output=True, timeout=90
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or 'Spotlight sync failed.')
    return completed.stdout.strip()

CONVERSATIONAL_FILLERS = [
    r"\bhey,?\s+(can you )?\b",
    r"\bi (remember( seeing)?|saw|read|noticed|wrote|have|saved|downloaded|opened|lost|learned about|studied|discussed|need)\b",
    r"\bwhere (did i read|did i study|did i put|is that explanation|is the explanation|is|are)\b",
    r"\b(can you )?(find( me)?|show( me)?|look for|search for)\b",
    r"\bfind that (thing|document|file|notes?|pdf|image|material)? (where|that|i)?\b",
    r"\bfind (every document|all documents?|other documents?|documents?|the document|the notes?|the file|the material|the pdf|the image|the scanned notes?|material that connects|notes about)\b",
    r"\bshow (me )?(all documents?|documents?|other documents?|material)\b",
    r"\bwhich (documents? discuss( both)?|file it was in|has|explains|discusses)\b",
    r"\bsomething (explaining|about|in|on|that says|like|related to)\b",
    r"\bnotes? (on|about|from|where i discussed|explaining)\b",
    r"\banything about\b",
    r"\ba file (called|named)?\b",
    r"\bdocuments? (on|about|explaining|containing|related to|that discuss|with)\b",
    r"\bwhat was (the|that)\b",
    r"\bfrom (my|the)\b",
    r"\bin (one of my|my|the)\b",
    r"\bthat (i opened|i was working on|i wrote|talks about|mentions|says|explains)\b",
    r"\bi (forgot|can't remember|cannot remember|don't remember) (which file it was in|(its|the) (name|title))\b",
    r"\bthere was (something|a diagram|a page|a formula) (about|explaining|with)?\b",
    r"\bsearch for (a page|a scanned document|a pdf|the pdf|an image|documents?)\b",
    r"\bplease\b",
]

# Words that describe the act of retrieving a file rather than its subject.
# These are applied only after a file-format intent has been detected, so a
# document genuinely titled "Search" is still searchable in normal queries.
FILE_REQUEST_FILLERS = re.compile(
    r"\b(?:show|find|search(?:\s+for)?|look\s+for|get|give|bring|list|display|"
    r"retrieve|locate|open|my|me|all|any|every|the|a|an|of|for|in|with|"
    r"images?|photos?|pictures?|screenshots?|scans?|files?|documents?|format|"
    r"type|extension|kind|ones?|items?)\b",
    re.IGNORECASE,
)


class MemoryQueryParser:
    """Parses natural, irregular, human-memory-style search queries."""

    @staticmethod
    def parse(raw_query: str, reference_time_epoch=None):
        if reference_time_epoch is None:
            reference_time_epoch = time.time()

        now = datetime.datetime.fromtimestamp(reference_time_epoch)
        text = raw_query.strip()

        typo_corrections = {}
        temporal_constraint = None
        file_type_constraint = None

        # File extensions are more specific than media words ("images of JPG").
        extension = re.search(r'\b(jpe?g|png|tiff?|bmp|webp|gif|pdf|md|markdown|txt)\b', text, re.I)
        if extension:
            ext = extension.group(1).lower()
            file_type_constraint = {'jpeg': 'jpg', 'tif': 'tiff', 'markdown': 'md'}.get(ext, ext)
        elif re.search(r'\b(pdf|document|slides?|presentation)\b', text, re.I):
            m = re.search(r'\bpdf\b', text, re.I)
            if m:
                file_type_constraint = 'pdf'
        elif re.search(r'\b(images?|photo|screenshot|scan|picture)\b', text, re.I):
            file_type_constraint = 'image'
        elif re.search(r'\b(markdown|md)\b', text, re.I):
            file_type_constraint = 'md'
        elif re.search(r'\b(text|txt|notes?)\b', text, re.I):
            file_type_constraint = 'txt'

        # 2. Extract temporal constraints. "Past" means a rolling interval;
        # "last week/month" means the preceding completed calendar period.
        date_match = re.search(r'\b(20\d{2})-(\d{1,2})-(\d{1,2})\b', text)
        if date_match:
            try:
                day = datetime.date(*map(int, date_match.groups()))
                start = datetime.datetime.combine(day, datetime.time.min).timestamp()
                end = datetime.datetime.combine(day + datetime.timedelta(days=1), datetime.time.min).timestamp() - 1
                temporal_constraint = {'label': day.strftime('%b %d, %Y'), 'start': start, 'end': end, 'weight': 4.0, 'description': f'Modified on {day.strftime("%b %d, %Y")}' }
                text = text.replace(date_match.group(0), ' ')
            except ValueError:
                pass
        # "last week" / "past week"
        if not temporal_constraint and re.search(r'\b(last|past) week\b', text, re.I):
            if re.search(r'\blast week\b', text, re.I):
                today = now.date()
                this_monday = today - datetime.timedelta(days=today.weekday())
                end_day = this_monday - datetime.timedelta(days=1)
                start_day = end_day - datetime.timedelta(days=6)
                start = datetime.datetime.combine(start_day, datetime.time.min).timestamp()
                end = datetime.datetime.combine(end_day + datetime.timedelta(days=1), datetime.time.min).timestamp() - 1
            else:
                start, end = reference_time_epoch - 7 * 86400, reference_time_epoch
            temporal_constraint = {
                'label': 'Last week',
                'start': start,
                'end': end,
                'weight': 3.5,
                'description': f'Modified around last week (~{(now - datetime.timedelta(days=7)).strftime("%b %d")})'
            }
            text = re.sub(r'\b(last|past) week\b', '', text, flags=re.I)
        # "yesterday"
        elif not temporal_constraint and re.search(r'\byesterday\b', text, re.I):
            start = reference_time_epoch - (2 * 86400)
            end = reference_time_epoch
            temporal_constraint = {
                'label': 'Yesterday',
                'start': start,
                'end': end,
                'weight': 4.0,
                'description': f'Modified yesterday ({(now - datetime.timedelta(days=1)).strftime("%b %d")})'
            }
            text = re.sub(r'\byesterday\b', '', text, flags=re.I)
        # "today"
        elif not temporal_constraint and re.search(r'\btoday\b', text, re.I):
            start = reference_time_epoch - 86400
            end = reference_time_epoch
            temporal_constraint = {
                'label': 'Today',
                'start': start,
                'end': end,
                'weight': 4.0,
                'description': f'Modified today ({now.strftime("%b %d")})'
            }
            text = re.sub(r'\btoday\b', '', text, flags=re.I)
        # "last month" / "past month"
        elif not temporal_constraint and re.search(r'\b(last|past) month\b', text, re.I):
            if re.search(r'\blast month\b', text, re.I):
                first_this_month = now.date().replace(day=1)
                last_month_end = first_this_month - datetime.timedelta(days=1)
                start_day = last_month_end.replace(day=1)
                start = datetime.datetime.combine(start_day, datetime.time.min).timestamp()
                end = datetime.datetime.combine(first_this_month, datetime.time.min).timestamp() - 1
            else:
                start, end = reference_time_epoch - 30 * 86400, reference_time_epoch
            temporal_constraint = {
                'label': 'Last month',
                'start': start,
                'end': end,
                'weight': 3.0,
                'description': 'Modified in the past month'
            }
            text = re.sub(r'\b(last|past) month\b', '', text, flags=re.I)
        # "recently" / "few days ago"
        elif not temporal_constraint and re.search(r'\b(recently|past few days|a few days ago|couple of days ago)\b', text, re.I):
            start = reference_time_epoch - (5 * 86400)
            end = reference_time_epoch
            temporal_constraint = {
                'label': 'Recently',
                'start': start,
                'end': end,
                'weight': 3.0,
                'description': 'Modified recently (past few days)'
            }
            text = re.sub(r'\b(recently|past few days|a few days ago|couple of days ago)\b', '', text, flags=re.I)
        # "in YYYY" (e.g. in 2024, in 2025, in 2026)
        year_match = re.search(r'\b(?:in\s+)?(202[0-9])\b', text, re.I)
        if year_match and not temporal_constraint:
            yr = int(year_match.group(1))
            try:
                start = datetime.datetime(yr, 1, 1).timestamp()
                end = datetime.datetime(yr, 12, 31, 23, 59, 59).timestamp()
                temporal_constraint = {
                    'label': f'In {yr}',
                    'start': start,
                    'end': end,
                    'weight': 2.5,
                    'description': f'Modified in year {yr}'
                }
                text = re.sub(r'\b(?:in\s+)?' + str(yr) + r'\b', '', text, flags=re.I)
            except Exception:
                pass

        # Remove file-request language from content terms. Keep topical words,
        # so "search my JPG images of a cat" searches JPG files about cats.
        cleaned = text
        if file_type_constraint:
            cleaned = FILE_REQUEST_FILLERS.sub(' ', cleaned)
            if file_type_constraint == 'jpg':
                cleaned = re.sub(r'\b(jpe?g)\b', ' ', cleaned, flags=re.I)
            else:
                cleaned = re.sub(r'\b' + re.escape(file_type_constraint) + r'\b', ' ', cleaned, flags=re.I)
        for filler in CONVERSATIONAL_FILLERS:
            cleaned = re.sub(filler, ' ', cleaned, flags=re.I)

        # 4. Extract specialized multi-word intent phrases before word tokenization
        lower_raw = raw_query.lower()
        extra_concepts = []
        if 'duplicate data' in lower_raw or 'prevents duplicate' in lower_raw:
            extra_concepts.extend(['unique', 'primary key', 'normalization', 'duplicate', 'constraint'])
        if 'source code into machine instructions' in lower_raw or 'converting source code' in lower_raw:
            extra_concepts.extend(['compiler', 'compilation', 'code generation', 'lexer', 'parser', 'machine instructions'])
        if 'shared resources safely' in lower_raw or 'multiple threads' in lower_raw:
            extra_concepts.extend(['synchronization', 'mutex', 'critical section', 'locks', 'threads', 'concurrency'])
        if 'temporarily store frequently used' in lower_raw or 'frequently used data' in lower_raw:
            extra_concepts.extend(['cache', 'caching', 'locality', 'l1', 'l2', 'memory hierarchy'])
        if 'relationships between tables' in lower_raw:
            extra_concepts.extend(['foreign key', 'relational', 'relationships', 'tables', 'schema', 'joins'])
        if 'handles runtime errors' in lower_raw or 'handles runtime' in lower_raw:
            extra_concepts.extend(['exception', 'try', 'catch', 'finally', 'runtime errors', 'throw'])
        if 'tree structure' in lower_raw or 'tree' in lower_raw:
            extra_concepts.extend(['binary search tree', 'tree', 'bst', 'traversal', 'nodes'])
        if 'external devices' in lower_raw or 'communicates with external' in lower_raw:
            extra_concepts.extend(['interrupts', 'io', 'device drivers', 'dma', 'peripherals'])
        if 'manages processes' in lower_raw or 'manage processes' in lower_raw:
            extra_concepts.extend(['pcb', 'process management', 'scheduling', 'context switch'])
        if 'authentication and authorization' in lower_raw or 'authentication' in lower_raw and 'authorization' in lower_raw:
            extra_concepts.extend(['authentication', 'authorization', 'authn', 'authz', 'permissions', 'rbac'])
        if 'ram and rom' in lower_raw:
            extra_concepts.extend(['ram', 'rom', 'volatile', 'read only memory', 'random access memory'])
        if 'standard deviation' in lower_raw:
            extra_concepts.extend(['standard deviation', 'variance', 'sigma', 'formula'])
        if 'correlation formula' in lower_raw or 'correlation' in lower_raw:
            extra_concepts.extend(['correlation', 'pearson', 'covariance', 'formula'])
        if 'third normal form' in lower_raw or '3nf' in lower_raw:
            extra_concepts.extend(['3nf', 'third normal form', 'normalization', 'transitive'])
        if 'try catch finally' in lower_raw or 'try catch' in lower_raw:
            extra_concepts.extend(['try', 'catch', 'finally', 'exception'])
        if 'osi model' in lower_raw or 'osi' in lower_raw:
            extra_concepts.extend(['osi', 'osi model', '7 layers', 'transport', 'network layer', 'data link'])
        if 'serialization and deserialization' in lower_raw:
            extra_concepts.extend(['serialization', 'deserialization', 'serializable', 'byte stream'])
        if 'hypothesis testing' in lower_raw:
            extra_concepts.extend(['hypothesis', 'null hypothesis', 'p-value', 't-test', 'significance'])
        if ('scanned' in lower_raw or 'handwritten' in lower_raw) and not re.search(r'\b(jpe?g|png|tiff?|bmp|webp|gif)\b', lower_raw):
            file_type_constraint = 'image'

        # 5. Tokenize words and correct typos
        raw_words = re.findall(r'[a-zA-Z0-9_\-]+', cleaned)
        corrected_words = []
        for word in raw_words:
            low = word.lower()
            if low in COMMON_TYPOS:
                corrected_words.append(COMMON_TYPOS[low])
                typo_corrections[word] = COMMON_TYPOS[low]
            else:
                corrected_words.append(low)

        # Stop words filter for core terms
        stop_words = {
            'a', 'an', 'the', 'and', 'or', 'in', 'on', 'at', 'to', 'for', 'of', 'with',
            'by', 'from', 'about', 'that', 'this', 'these', 'those', 'is', 'was', 'are',
            'were', 'be', 'been', 'being', 'have', 'has', 'had', 'do', 'does', 'did',
            'my', 'your', 'his', 'her', 'its', 'our', 'their', 'me', 'him', 'them',
            'what', 'which', 'who', 'whom', 'where', 'when', 'why', 'how', 'it', 'some',
            'between', 'into', 'differ', 'difference', 'need', 'needed', 'explaining',
            'explanation', 'discuss', 'discusses', 'discussed', 'discussing', 'learned',
            'learning', 'studied', 'study', 'work', 'working', 'works', 'say', 'says',
            'containing', 'contains', 'heading', 'mentions', 'mention', 'phrase', 'words',
            'get', 'all', 'image', 'images', 'photo', 'photos', 'picture', 'pictures',
            'jpg', 'jpeg', 'png', 'tiff', 'tif', 'bmp', 'webp', 'gif', 'format', 'file', 'files',
            'type', 'show', 'find', 'give', 'me'
        }
        core_terms = [w for w in corrected_words if w not in stop_words and len(w) > 1]
        if temporal_constraint:
            temporal_words = {'document', 'documents', 'file', 'files', 'modified', 'created', 'saved', 'opened', 'downloaded', 'changed', 'made', 'from', 'on', 'during'}
            core_terms = [word for word in core_terms if word not in temporal_words]

        # 6. Extract semantic concepts
        semantic_concepts = list(extra_concepts)
        for term in core_terms:
            for synonym in QUERY_SYNONYMS.get(term, []):
                if synonym not in semantic_concepts and synonym not in core_terms:
                    semantic_concepts.append(synonym)
            if term in CONCEPT_TAXONOMY:
                for exp in CONCEPT_TAXONOMY[term]:
                    if exp not in semantic_concepts and exp not in core_terms:
                        semantic_concepts.append(exp)

        # Combine terms for C++ engine query
        if core_terms:
            search_query_engine = " ".join(core_terms)
        elif file_type_constraint or temporal_constraint:
            search_query_engine = ""
        else:
            search_query_engine = " ".join(raw_words) if raw_words else raw_query

        # Build natural explanation of how the system understood the query
        explanation_parts = []
        if core_terms:
            explanation_parts.append(f"Focusing on concepts: <b>{', '.join(core_terms)}</b>")
        if typo_corrections:
            corrections_str = ", ".join(f"{k} → <i>{v}</i>" for k, v in typo_corrections.items())
            explanation_parts.append(f"Corrected spelling: {corrections_str}")
        if temporal_constraint:
            explanation_parts.append(f"Time filter: <b>{temporal_constraint['label']}</b> ({temporal_constraint['description']})")
        if file_type_constraint:
            explanation_parts.append(f"File request: <b>{file_type_constraint.upper()}</b> format")
        if semantic_concepts:
            preview_concepts = semantic_concepts[:5]
            explanation_parts.append(f"Semantic expansions: {', '.join(preview_concepts)}")

        return {
            'raw_query': raw_query,
            'intent': 'file_filter' if file_type_constraint and not core_terms else 'memory_search',
            'search_query_engine': search_query_engine,
            'core_terms': core_terms,
            'typo_corrections': typo_corrections,
            'temporal_constraint': temporal_constraint,
            'file_type_constraint': file_type_constraint,
            'semantic_concepts': semantic_concepts,
            'explanation_html': " · ".join(explanation_parts) if explanation_parts else "Direct keyword search"
        }


# ---------------------------------------------------------------------------
# Background Indexing Manager
# ---------------------------------------------------------------------------

class BackgroundIndexer:
    def __init__(self):
        self.lock = threading.Lock()
        self.status = 'idle'  # 'idle', 'indexing', 'completed', 'error'
        self.folder = ''
        self.current_file = ''
        self.processed = 0
        self.total = 0
        self.percent = 0
        self.logs = []
        self.error_message = ''
        self.process = None

    def start(self, folder_path, db_path):
        with self.lock:
            if self.status == 'indexing':
                return False, 'An indexing job is already in progress.'
            self.status = 'indexing'
            self.folder = str(folder_path)
            self.current_file = 'Scanning folder...'
            self.processed = 0
            self.total = 0
            self.percent = 0
            self.logs = [f"Started indexing folder: {folder_path}"]
            self.error_message = ''

        thread = threading.Thread(target=self._run, args=(folder_path, db_path), daemon=True)
        thread.start()
        return True, 'Indexing started in background.'

    def _run(self, folder_path, db_path):
        try:
            cmd = [str(ENGINE), 'index', str(folder_path), '--db', str(db_path)]
            self.process = subprocess.Popen(
                cmd, cwd=str(ROOT),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1
            )
            for line in iter(self.process.stdout.readline, ''):
                line = line.strip()
                if line:
                    with self.lock:
                        self.logs.append(line)
                        if len(self.logs) > 200:
                            self.logs.pop(0)
                        if 'New files:' in line or 'Files extracted:' in line:
                            self.current_file = line
                        elif 'Note:' in line:
                            self.current_file = line
            self.process.stdout.close()
            return_code = self.process.wait()
            with self.lock:
                if return_code == 0:
                    self.status = 'completed'
                    self.percent = 100
                    self.current_file = 'Indexing completed successfully.'
                    self.logs.append('Indexing completed successfully.')
                    # Post-process SQLite documents to record extraction methods and page counts
                    try:
                        self._enrich_metadata(db_path)
                    except Exception:
                        pass
                    if sys.platform == 'darwin' and SPOTLIGHT_BRIDGE.exists():
                        try:
                            sync_spotlight_index()
                            self.logs.append('Spotlight metadata synchronized.')
                        except Exception as exc:
                            self.logs.append(f'Spotlight synchronization skipped: {exc}')
                else:
                    self.status = 'error'
                    self.error_message = f'Engine exited with code {return_code}'
                    self.logs.append(self.error_message)
        except Exception as exc:
            with self.lock:
                self.status = 'error'
                self.error_message = str(exc)
                self.logs.append(f"Error: {exc}")
        finally:
            self.process = None

    def _enrich_metadata(self, db_path):
        conn = sqlite3.connect(str(db_path))
        ensure_schema(conn)
        cur = conn.cursor()
        cur.execute("SELECT id, file_path, file_type, extracted_text FROM documents")
        rows = cur.fetchall()
        for doc_id, fpath, ftype, text in rows:
            # Detect extraction method
            ftype_l = (ftype or '').lower()
            if ftype_l == 'pdf':
                # Check if it was native or OCR
                method = 'Native PDF (Poppler)'
            elif ftype_l in ('png', 'jpg', 'jpeg', 'tif', 'tiff', 'bmp', 'webp', 'gif'):
                method = 'OCR (Tesseract)'
            else:
                method = 'Plain Text'

            # Estimate page count from text length or markers
            pages = max(1, text.count('\x0c') + 1)
            if pages == 1 and len(text) > 3000:
                pages = max(1, int(len(text) / 2500) + 1)

            cur.execute("""
                UPDATE documents
                SET extraction_method = ?, page_count = ?, indexed_at_epoch_seconds = ?
                WHERE id = ? AND (extraction_method IS NULL OR extraction_method = '')
            """, (method, pages, int(time.time()), doc_id))
        conn.commit()
        conn.close()

    def get_progress(self):
        with self.lock:
            return {
                'status': self.status,
                'folder': self.folder,
                'current_file': self.current_file,
                'processed': self.processed,
                'total': self.total,
                'percent': self.percent,
                'logs': self.logs[-40:],
                'error': self.error_message
            }

    def cancel(self):
        with self.lock:
            if self.process and self.status == 'indexing':
                self.process.terminate()
                self.status = 'idle'
                self.logs.append('Indexing cancelled by user.')
                return True
        return False


indexer = BackgroundIndexer()


# ---------------------------------------------------------------------------
# Snippet & Passage Highlight Generator
# ---------------------------------------------------------------------------

def extract_best_passage(text: str, query_terms: list, concept_terms: list, max_length=280):
    if not text:
        return {'passage': '', 'page': 1, 'highlighted_html': ''}

    # Detect pages by form feed (\x0c or \f) or page breaks
    pages = text.split('\x0c')
    if len(pages) <= 1:
        # Split by virtual pages (~2500 characters) if long
        pages = [text[i:i+2500] for i in range(0, len(text), 2500)] if len(text) > 3000 else [text]

    all_terms = [t.lower() for t in query_terms if len(t) > 1]
    all_concepts = [c.lower() for c in concept_terms if len(c) > 1 and c not in all_terms]

    best_page_idx = 0
    best_page_score = -1
    best_chunk = ""

    for p_idx, page in enumerate(pages):
        page_lower = page.lower()
        score = 0
        for t in all_terms:
            score += page_lower.count(t) * 3
        for c in all_concepts:
            score += page_lower.count(c) * 1

        if score > best_page_score:
            best_page_score = score
            best_page_idx = p_idx

    target_text = pages[best_page_idx]

    # Find the earliest occurrence of a primary query term
    first_pos = -1
    target_lower = target_text.lower()
    for t in all_terms:
        idx = target_lower.find(t)
        if idx != -1 and (first_pos == -1 or idx < first_pos):
            first_pos = idx

    if first_pos == -1:
        for c in all_concepts:
            idx = target_lower.find(c)
            if idx != -1 and (first_pos == -1 or idx < first_pos):
                first_pos = idx

    if first_pos == -1:
        # Just grab the beginning
        chunk = target_text[:max_length].strip()
    else:
        start = max(0, first_pos - 80)
        end = min(len(target_text), start + max_length)
        # expand to whitespace
        if start > 0:
            sp = target_text.find(' ', start)
            if sp != -1 and sp < start + 20:
                start = sp + 1
        chunk = target_text[start:end].strip()
        if start > 0:
            chunk = "..." + chunk
        if end < len(target_text):
            chunk = chunk + "..."

    # Highlight matches in HTML
    # Escape HTML special chars
    escaped = chunk.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')

    # Highlight primary terms with <mark class="hl-primary">
    for t in sorted(all_terms, key=len, reverse=True):
        pattern = re.compile(rf'\b({re.escape(t)})\b', re.IGNORECASE)
        escaped = pattern.sub(r'<mark class="hl-primary">\1</mark>', escaped)

    # Highlight concept terms with <mark class="hl-concept">
    for c in sorted(all_concepts[:8], key=len, reverse=True):
        pattern = re.compile(rf'\b({re.escape(c)})\b', re.IGNORECASE)
        escaped = pattern.sub(r'<mark class="hl-concept">\1</mark>', escaped)

    return {
        'passage': chunk,
        'page': best_page_idx + 1,
        'highlighted_html': escaped
    }


# ---------------------------------------------------------------------------
# Intelligent Search Handler
# ---------------------------------------------------------------------------

def perform_intelligent_search(raw_query: str, filters: dict = None):
    parsed = MemoryQueryParser.parse(raw_query)
    search_term = parsed['search_query_engine']

    # 1. Run the C++ engine search with JSON output
    engine_results = []
    engine_related = []
    if ENGINE.exists() and search_term:
        try:
            cmd = [str(ENGINE), 'search', search_term, '--db', str(DB), '--max-results', '5000', '--json']
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=30)
            if proc.returncode == 0 and proc.stdout.strip():
                try:
                    data = json.loads(proc.stdout)
                    engine_results = data.get('results', [])
                    engine_related = data.get('related', [])
                except Exception:
                    pass
        except Exception:
            pass

    # When literal topic terms have no hits, try a bounded transparent synonym
    # query. Primary exact-term matches always take precedence when available.
    if ENGINE.exists() and not engine_results and parsed['semantic_concepts']:
        expansion_query = ' '.join(parsed['semantic_concepts'][:6])
        try:
            proc = subprocess.run(
                [str(ENGINE), 'search', expansion_query, '--db', str(DB), '--max-results', '5000', '--json'],
                cwd=ROOT, text=True, capture_output=True, timeout=30
            )
            if proc.returncode == 0 and proc.stdout.strip():
                expanded = json.loads(proc.stdout)
                engine_results = expanded.get('results', [])
                engine_related = expanded.get('related', [])
                for result in engine_results:
                    result['matchedBy'] = 'Related wording: ' + expansion_query
        except Exception:
            pass

    # 2. Query SQLite directly for all documents to allow hybrid re-ranking,
    # title/filename searching, metadata matching, and temporal scoring
    conn = get_db_connection()
    cur = conn.cursor()
    cur.execute("""
        SELECT id, title, file_path, file_type, size_bytes,
               modified_at_epoch_seconds, tags, user_notes, extraction_method, page_count
        FROM documents
    """)
    all_docs = {row['id']: dict(row) for row in cur.fetchall()}
    semantic_scores = semantic_candidates(raw_query) if parsed['core_terms'] else {}

    # Merge C++ results with SQLite document records
    candidates_by_id = {}
    for r in engine_results:
        doc_id = r['id']
        doc = all_docs.get(doc_id, {})
        candidates_by_id[doc_id] = {
            'id': doc_id,
            'title': r.get('title') or doc.get('title', ''),
            'file_path': r.get('filePath') or doc.get('file_path', ''),
            'file_type': r.get('fileType') or doc.get('file_type', ''),
            'size_bytes': r.get('sizeBytes') or doc.get('size_bytes', 0),
            'modified_at_epoch_seconds': r.get('modifiedAtEpochSeconds') or doc.get('modified_at_epoch_seconds'),
            'tags': doc.get('tags', ''),
            'user_notes': doc.get('user_notes', ''),
            'extraction_method': doc.get('extraction_method', 'Native Text'),
            'page_count': doc.get('page_count', 1),
            'extracted_text': '',
            'base_score': float(r.get('score', 0.0)),
            'matched_by': r.get('matchedBy', ''),
            'breakdown': r.get('breakdown', {}),
            'c_engine_hit': True
        }

    # Semantic candidates use meaning similarity from the local embedding model.
    # They supplement lexical evidence; exact keywords still contribute through
    # the C++ engine and the final ranking pass below.
    for doc_id, semantic_score in semantic_scores.items():
        doc = all_docs.get(doc_id)
        if not doc:
            continue
        if doc_id in candidates_by_id:
            candidates_by_id[doc_id]['semantic_score'] = semantic_score
            continue
        candidates_by_id[doc_id] = {
            'id': doc_id, 'title': doc.get('title', ''), 'file_path': doc.get('file_path', ''),
            'file_type': doc.get('file_type', ''), 'size_bytes': doc.get('size_bytes', 0),
            'modified_at_epoch_seconds': doc.get('modified_at_epoch_seconds'), 'tags': doc.get('tags', ''),
            'user_notes': doc.get('user_notes', ''), 'extraction_method': doc.get('extraction_method', 'Native Text'),
            'page_count': doc.get('page_count', 1), 'extracted_text': '',
            'base_score': 0.0, 'matched_by': 'Local AI meaning match', 'breakdown': {},
            'c_engine_hit': False, 'semantic_score': semantic_score
        }

    # Also search directly in SQLite for documents whose title, path, tags, or notes
    # match any core terms (catches cases where terms exist in filenames or metadata)
    core_terms = parsed['core_terms']
    type_constraint = parsed['file_type_constraint']
    temporal = parsed['temporal_constraint']
    for doc_id, doc in all_docs.items():
        if doc_id in candidates_by_id:
            continue
        title_l = (doc.get('title') or '').lower()
        path_l = (doc.get('file_path') or '').lower()
        tags_l = (doc.get('tags') or '').lower()
        notes_l = (doc.get('user_notes') or '').lower()
        match_score = 0.0
        match_reasons = []
        for term in core_terms:
            t_low = term.lower()
            is_in = lambda value: re.search(r'(?<![a-z0-9])' + re.escape(t_low) + r'(?![a-z0-9])', value) is not None
            if is_in(title_l):
                match_score += 4.5
                match_reasons.append(f"Title contains '{term}'")
            elif is_in(path_l):
                match_score += 3.0
                match_reasons.append(f"Path contains '{term}'")
            elif is_in(tags_l):
                match_score += 4.0
                match_reasons.append(f"Tagged with '{term}'")
            elif is_in(notes_l):
                match_score += 3.5
                match_reasons.append(f"Notes contain '{term}'")

        if match_score > 0.0:
            candidates_by_id[doc_id] = {
                'id': doc_id,
                'title': doc.get('title', ''),
                'file_path': doc.get('file_path', ''),
                'file_type': doc.get('file_type', ''),
                'size_bytes': doc.get('size_bytes', 0),
                'modified_at_epoch_seconds': doc.get('modified_at_epoch_seconds'),
                'tags': doc.get('tags', ''),
                'user_notes': doc.get('user_notes', ''),
                'extraction_method': doc.get('extraction_method', 'Native Text'),
                'page_count': doc.get('page_count', 1),
                'extracted_text': '',
                'base_score': match_score,
                'matched_by': "; ".join(match_reasons),
                'breakdown': {'directMetadata': match_score},
                'c_engine_hit': False
            }

    # A format-only human query means "show files of this format"; it should not
    # depend on extracted OCR/text containing words such as "images" or "jpg".
    if (type_constraint or temporal) and not core_terms:
        allowed_types = {'image': {'png','jpg','jpeg','tif','tiff','bmp','webp','gif'}, 'jpg': {'jpg','jpeg'}, 'tiff': {'tif','tiff'}, 'md': {'md','markdown'}, 'txt': {'txt','text'}}
        for doc_id, doc in all_docs.items():
            kind = (doc.get('file_type') or '').lower()
            type_matches = not type_constraint or kind in allowed_types.get(type_constraint, {type_constraint})
            date_value = doc.get('modified_at_epoch_seconds')
            date_matches = not temporal or (date_value is not None and temporal['start'] <= date_value <= temporal['end'])
            if type_matches and date_matches:
                candidates_by_id.setdefault(doc_id, {
                    'id': doc_id, 'title': doc.get('title', ''), 'file_path': doc.get('file_path', ''),
                    'file_type': doc.get('file_type', ''), 'size_bytes': doc.get('size_bytes', 0),
                    'modified_at_epoch_seconds': doc.get('modified_at_epoch_seconds'), 'tags': doc.get('tags', ''),
                    'user_notes': doc.get('user_notes', ''), 'extraction_method': doc.get('extraction_method', 'Native Text'),
                    'page_count': doc.get('page_count', 1), 'extracted_text': doc.get('extracted_text', ''),
                    'base_score': 1.0, 'matched_by': 'Format match', 'breakdown': {}, 'c_engine_hit': False
                })

    # Fetch large extracted bodies only for the small candidate set used for
    # passage generation and final scoring, rather than materializing every body.
    for doc_id, candidate in candidates_by_id.items():
        cur.execute('SELECT extracted_text FROM documents WHERE id = ?', (doc_id,))
        row = cur.fetchone()
        candidate['extracted_text'] = row[0] if row else ''

    # 3. Apply Filters and Memory Re-Ranking
    now_epoch = time.time()

    scored_results = []
    for doc_id, item in candidates_by_id.items():
        total_score = item['base_score']
        why_parts = []
        temporal_matched = False
        doc_type = (item.get('file_type') or '').lower()

        if item.get('matched_by'):
            why_parts.append(f"Matches search terms ({item['matched_by']})")

        semantic_score = item.get('semantic_score')
        if semantic_score is not None:
            total_score += max(0.0, semantic_score) * 28.0
            why_parts.append('Local AI found a related meaning')

        # Discount third-party dependency / node_modules noise
        fpath_l = (item.get('file_path') or '').lower()
        title_l = (item.get('title') or '').lower()
        text_l = (item.get('extracted_text') or '').lower()
        full_doc_text = f"{title_l} {fpath_l} {text_l}"

        if 'node_modules' in fpath_l or 'cmake-build' in fpath_l or '_deps' in fpath_l or '.git' in fpath_l:
            total_score *= 0.12

        # Reward complete, whole-token coverage instead of substring coincidences.
        if core_terms:
            doc_tokens = set(re.findall(r'[a-zA-Z0-9]+', full_doc_text))
            terms_present = sum(1 for term in core_terms if term.lower() in doc_tokens)
            coverage_ratio = terms_present / len(core_terms)
            total_score += coverage_ratio * 35.0
            if coverage_ratio == 1.0 and len(core_terms) > 1:
                total_score += 25.0
                why_parts.append(f"All {len(core_terms)} key recollection terms present")

        # Exact multi-term phrase matches
        if len(core_terms) >= 2:
            phrase_str = " ".join(core_terms)
            if phrase_str in full_doc_text:
                total_score += 40.0
                why_parts.append(f"Exact phrase '{phrase_str}' found in document")

        # Temporal fit scoring
        mtime = item.get('modified_at_epoch_seconds')
        if temporal and not mtime:
            continue
        if temporal and mtime:
            t_start = temporal['start']
            t_end = temporal['end']
            if not (t_start <= mtime <= t_end):
                continue
            total_score += 18.0 * temporal['weight']
            temporal_matched = True
            date_str = datetime.datetime.fromtimestamp(mtime).strftime("%b %d, %Y")
            why_parts.append(f"Date match: {temporal['label']} (modified {date_str})")

        # File type filter & boost
        doc_type = (item.get('file_type') or '').lower()
        if type_constraint:
            aliases = {'image': {'png','jpg','jpeg','tif','tiff','bmp','webp','gif'}, 'md': {'md','markdown'}, 'txt': {'txt','text'}, 'jpg': {'jpg','jpeg'}, 'tiff': {'tif','tiff'}}
            accepted = aliases.get(type_constraint, {type_constraint})
            if doc_type not in accepted:
                continue
            why_parts.append(f"Matches {type_constraint.upper()} format requirement")

        # Explicit UI filters
        if filters:
            if filters.get('file_type') and filters['file_type'] != 'all':
                selected_type = filters['file_type'].lower()
                aliases = {'image': {'png','jpg','jpeg','tif','tiff','bmp','webp','gif'}, 'md': {'md','markdown'}, 'txt': {'txt','text'}}
                if doc_type not in aliases.get(selected_type, {selected_type}):
                    continue
            if filters.get('date_range') and mtime:
                dr = filters['date_range']
                if dr == 'day' and now_epoch - mtime > 86400:
                    continue
                elif dr == 'week' and now_epoch - mtime > (7 * 86400):
                    continue
                elif dr == 'month' and now_epoch - mtime > (30 * 86400):
                    continue
                elif dr == 'year' and now_epoch - mtime > (365 * 86400):
                    continue

        # Semantic concept density boost
        text = item.get('extracted_text', '')
        concept_matches = 0
        for concept in parsed['semantic_concepts']:
            if concept in text.lower():
                concept_matches += 1
        if concept_matches > 0:
            concept_boost = min(12.0, concept_matches * 2.2)
            total_score += concept_boost
            why_parts.append(f"Connected to {concept_matches} related concepts")

        # Extract passage & page reference
        passage_info = extract_best_passage(text, core_terms, parsed['semantic_concepts'])

        # Modification date string
        mod_date_str = ""
        if mtime:
            mod_date_str = datetime.datetime.fromtimestamp(mtime).strftime("%b %d, %Y")

        scored_results.append({
            'id': doc_id,
            'title': item['title'],
            'file_path': item['file_path'],
            'file_type': item['file_type'],
            'size_bytes': item['size_bytes'],
            'modified_at_epoch_seconds': mtime,
            'modified_date_str': mod_date_str,
            'tags': item.get('tags', ''),
            'user_notes': item.get('user_notes', ''),
            'extraction_method': item.get('extraction_method', 'Native Text'),
            'page_count': item.get('page_count', 1),
            'matched_page': passage_info['page'],
            'passage': passage_info['passage'],
            'passage_html': passage_info['highlighted_html'],
            'score': round(total_score, 2),
            'temporal_matched': temporal_matched,
            'why_selected': " · ".join(why_parts) if why_parts else "Keyword match in document",
        })

    # Sort by total memory score descending
    scored_results.sort(key=lambda x: x['score'], reverse=True)

    # Calculate match percentage confidence (0-100%)
    if scored_results:
        max_score = scored_results[0]['score'] or 1.0
        for idx, r in enumerate(scored_results):
            r['rank'] = idx + 1
            conf = int(min(99, max(45, (r['score'] / max_score) * 98)))
            r['confidence_pct'] = conf

    return {
        'interpretation': parsed,
        'results': scored_results[:20],
        'related': engine_related,
        'total_found': len(scored_results)
    }


# ---------------------------------------------------------------------------
# HTTP Request Handler
# ---------------------------------------------------------------------------

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, fmt, *args):
        print('[AFTERIMAGE UI] ' + fmt % args)

    def send_json(self, data, status=200):
        body = json.dumps(data, default=str).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        query_params = urllib.parse.parse_qs(parsed_url.query)

        if path == '/api/status':
            db_path = get_database_path()
            self.send_json({
                'ready': ENGINE.exists(),
                'engine_path': str(ENGINE),
                'database': str(db_path),
                'database_exists': db_path.exists(),
                'database_bytes': db_path.stat().st_size if db_path.exists() else 0
            })
        elif path == '/api/stats':
            self.handle_stats()
        elif path == '/api/document':
            self.handle_document(query_params)
        elif path == '/api/table':
            self.handle_table(query_params)
        elif path == '/api/index/progress':
            self.send_json(indexer.get_progress())
        elif path == '/api/semantic/status':
            self.send_json(semantic_indexer.snapshot())
        else:
            super().do_GET()

    def do_POST(self):
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if length > 262144:  # 256KB max
                return self.send_json({'error': 'Request body too large.'}, 413)
            data = json.loads(self.rfile.read(length) or b'{}')

            if self.path == '/api/search':
                self.handle_search(data)
            elif self.path == '/api/index':
                self.handle_index(data)
            elif self.path == '/api/index/cancel':
                self.send_json({'cancelled': indexer.cancel()})
            elif self.path == '/api/semantic/index':
                started, message = semantic_indexer.start()
                self.send_json({'started': started, 'message': message}, 200 if started else 409)
            elif self.path == '/api/spotlight/sync':
                self.send_json({'message': sync_spotlight_index()})
            elif self.path == '/api/metadata':
                self.handle_update_metadata(data)
            elif self.path == '/api/sql':
                self.handle_sql_query(data)
            elif self.path == '/api/open':
                self.handle_open_file(data)
            else:
                self.send_json({'error': 'Not found.'}, 404)
        except Exception as exc:
            self.send_json({'error': str(exc)}, 500)

    def handle_search(self, data):
        query = str(data.get('query', '')).strip()
        if not query:
            return self.send_json({'error': 'Please enter a search query or memory fragment.'}, 400)
        filters = data.get('filters', {})
        if data.get('mode') == 'exact':
            return self.send_json(_exact_word_search(query, filters))
        result = perform_intelligent_search(query, filters)
        self.send_json(result)

    def handle_stats(self):
        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*), SUM(size_bytes), SUM(LENGTH(extracted_text)) FROM documents")
        doc_count, total_size, total_chars = cur.fetchone()

        cur.execute("SELECT file_type, COUNT(*) FROM documents GROUP BY file_type ORDER BY COUNT(*) DESC")
        types_breakdown = [{'type': row[0] or 'unknown', 'count': row[1]} for row in cur.fetchall()]

        cur.execute("SELECT extraction_method, COUNT(*) FROM documents GROUP BY extraction_method")
        methods_breakdown = [{'method': row[0] or 'Native Text', 'count': row[1]} for row in cur.fetchall()]
        cur.execute('SELECT COUNT(*) FROM semantic_vectors')
        semantic_vector_count = cur.fetchone()[0]

        # Call C++ stats if available
        c_stats = {}
        if ENGINE.exists():
            try:
                proc = subprocess.run([str(ENGINE), 'stats', '--db', str(DB), '--json'],
                                      cwd=ROOT, text=True, capture_output=True, timeout=10)
                if proc.returncode == 0:
                    c_stats = json.loads(proc.stdout)
            except Exception:
                pass

        self.send_json({
            'documents': doc_count or 0,
            'total_size_bytes': total_size or 0,
            'total_extracted_characters': total_chars or 0,
            'types_breakdown': types_breakdown,
            'methods_breakdown': methods_breakdown,
            'vocabulary': c_stats.get('vocabulary', 0),
            'postings': c_stats.get('postings', 0),
            'graph_concepts': c_stats.get('concepts', 0),
            'graph_edges': c_stats.get('edges', 0),
            'semantic_vectors': semantic_vector_count,
            'semantic_model': EMBEDDING_MODEL,
            'database': str(DB)
        })

    def handle_document(self, query_params):
        doc_id = query_params.get('id', [None])[0]
        if not doc_id:
            return self.send_json({'error': 'Document ID required.'}, 400)

        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute("""
            SELECT id, title, file_path, file_type, extracted_text, size_bytes,
                   modified_at_epoch_seconds, tags, user_notes, extraction_method, page_count
            FROM documents WHERE id = ?
        """, (doc_id,))
        row = cur.fetchone()
        if not row:
            return self.send_json({'error': 'Document not found.'}, 404)

        doc = dict(row)
        text = doc.get('extracted_text', '')

        # Page-by-page breakdown
        pages = text.split('\x0c')
        if len(pages) <= 1 and len(text) > 3000:
            pages = [text[i:i+2500] for i in range(0, len(text), 2500)]
        elif len(pages) <= 1:
            pages = [text]

        doc['pages'] = [{'page_number': i + 1, 'text': p} for i, p in enumerate(pages)]
        doc['total_pages'] = len(pages)
        if doc.get('modified_at_epoch_seconds'):
            doc['modified_date_str'] = datetime.datetime.fromtimestamp(doc['modified_at_epoch_seconds']).strftime("%b %d, %Y %I:%M %p")
        self.send_json(doc)

    def handle_table(self, query_params):
        page = max(1, int(query_params.get('page', [1])[0]))
        limit = min(100, max(5, int(query_params.get('limit', [20])[0])))
        offset = (page - 1) * limit
        search_filter = query_params.get('filter', [''])[0].strip()

        conn = get_db_connection()
        cur = conn.cursor()

        if search_filter:
            like_term = f"%{search_filter}%"
            cur.execute("""
                SELECT COUNT(*) FROM documents
                WHERE title LIKE ? OR file_path LIKE ? OR tags LIKE ?
            """, (like_term, like_term, like_term))
            total = cur.fetchone()[0]

            cur.execute("""
                SELECT id, title, file_path, file_type, size_bytes,
                       modified_at_epoch_seconds, tags, extraction_method, page_count
                FROM documents
                WHERE title LIKE ? OR file_path LIKE ? OR tags LIKE ?
                ORDER BY id DESC LIMIT ? OFFSET ?
            """, (like_term, like_term, like_term, limit, offset))
        else:
            cur.execute("SELECT COUNT(*) FROM documents")
            total = cur.fetchone()[0]

            cur.execute("""
                SELECT id, title, file_path, file_type, size_bytes,
                       modified_at_epoch_seconds, tags, extraction_method, page_count
                FROM documents
                ORDER BY id DESC LIMIT ? OFFSET ?
            """, (limit, offset))

        rows = [dict(r) for r in cur.fetchall()]
        for r in rows:
            if r.get('modified_at_epoch_seconds'):
                r['modified_date_str'] = datetime.datetime.fromtimestamp(r['modified_at_epoch_seconds']).strftime("%b %d, %Y")

        self.send_json({
            'page': page,
            'limit': limit,
            'total': total,
            'total_pages': math.ceil(total / limit) if total else 1,
            'rows': rows
        })

    def handle_update_metadata(self, data):
        doc_id = data.get('id')
        if not doc_id:
            return self.send_json({'error': 'Document ID is required.'}, 400)

        conn = get_db_connection()
        cur = conn.cursor()
        title = data.get('title')
        tags = data.get('tags')
        notes = data.get('user_notes')

        updates = []
        params = []
        if title is not None:
            updates.append("title = ?")
            params.append(str(title).strip())
        if tags is not None:
            updates.append("tags = ?")
            params.append(str(tags).strip())
        if notes is not None:
            updates.append("user_notes = ?")
            params.append(str(notes).strip())

        if not updates:
            return self.send_json({'error': 'No fields to update.'}, 400)

        params.append(doc_id)
        cur.execute(f"UPDATE documents SET {', '.join(updates)} WHERE id = ?", params)
        conn.commit()
        self.send_json({'success': True, 'id': doc_id})

    def handle_sql_query(self, data):
        query = str(data.get('sql', '')).strip()
        if not query:
            return self.send_json({'error': 'SQL query required.'}, 400)

        # Allow SELECT and PRAGMA queries only for security
        first_word = query.split()[0].upper()
        if first_word not in ('SELECT', 'PRAGMA', 'EXPLAIN'):
            return self.send_json({'error': 'Developer console only permits read-only SELECT or PRAGMA queries.'}, 403)

        conn = get_db_connection()
        cur = conn.cursor()
        try:
            cur.execute(query)
            columns = [desc[0] for desc in cur.description] if cur.description else []
            rows = [list(r) for r in cur.fetchmany(100)]
            self.send_json({
                'columns': columns,
                'rows': rows,
                'row_count': len(rows),
                'truncated': len(rows) == 100
            })
        except Exception as exc:
            self.send_json({'error': str(exc)}, 400)

    def handle_index(self, data):
        folder = Path(str(data.get('folder', ''))).expanduser().resolve()
        if not folder.is_dir():
            return self.send_json({'error': 'Please provide an existing folder path.'}, 400)

        started, message = indexer.start(folder, DB)
        if not started:
            return self.send_json({'error': message}, 409)
        self.send_json({'message': message, 'folder': str(folder)})

    def handle_open_file(self, data):
        target = Path(str(data.get('path', ''))).expanduser().resolve()
        if not target.is_file():
            return self.send_json({'error': 'That file is no longer available.'}, 404)

        if sys.platform == 'darwin':
            subprocess.Popen(['open', str(target)])
        elif os.name == 'nt':
            os.startfile(str(target))
        else:
            subprocess.Popen(['xdg-open', str(target)])
        self.send_json({'opened': True, 'path': str(target)})


# ---------------------------------------------------------------------------
# Server Entry Point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Launch the local AFTERIMAGE Memory Engine')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()

    db_path = get_database_path()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = get_db_connection()
    conn.close()

    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'AFTERIMAGE Memory Retrieval System running at http://127.0.0.1:{args.port}')
    print(f'Database: {db_path}')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nStopping AFTERIMAGE.')
