# Database Management Systems: Normalization, Integrity, and SQL Joins

## 1. Why Normalization is Needed in Databases
Database normalization is the formal process of structuring a relational database schema to minimize data redundancy and prevent data anomalies. Without normalization, databases suffer from:
- **Insertion Anomalies**: Inability to record certain facts without adding unrelated attributes.
- **Update Anomalies**: Changing a value in one place leaves duplicate entries inconsistent.
- **Deletion Anomalies**: Removing one piece of information unintentionally erases other vital records.

## 2. How a Database Prevents Duplicate Data
Relational databases enforce data uniqueness and prevent duplicate rows using schema constraints:
- **Primary Keys**: A column (or set of columns) that uniquely identifies each record in a table. It cannot contain `NULL` values.
- **Unique Constraints (`UNIQUE`)**: Guarantees that all values in a column or composite group are distinct across all records.
- **Candidate Keys**: Minimal sets of attributes capable of uniquely identifying tuples.
- **Entity Integrity**: Guarantees every row has a distinct, valid identity.

## 3. The Normal Forms
Database designs progress through formal normal forms:
- **First Normal Form (1NF)**: All column values must be atomic (no repeating groups, multi-valued attributes, or array columns).
- **Second Normal Form (2NF)**: The table is in 1NF and contains no partial dependencies; every non-prime attribute is fully functionally dependent on the entire primary key.
- **Third Normal Form (3NF)**: The table is in 2NF and contains no transitive dependencies. If $X \rightarrow Y$ and $Y \rightarrow Z$, where $X$ is a candidate key, then $Z$ must not functionally depend on $Y$. Every non-prime attribute must depend directly on "the key, the whole key, and nothing but the key".
- **Boyce-Codd Normal Form (BCNF)**: A stricter version of 3NF where for every functional dependency $X \rightarrow Y$, $X$ must be a superkey.

## 4. Relationships Between Tables in a Relational Database
Tables in a relational schema connect through mathematical relations and foreign key references:
- **Foreign Key (`FOREIGN KEY`)**: A field in one table that references the primary key of another table, establishing referential integrity.
- **One-to-Many Relationships**: The most common relationship (e.g. one `Customer` has many `Orders`).
- **Many-to-Many Relationships**: Implemented using an associative junction table with two foreign keys.

## 5. SQL Joins
SQL queries combine rows from two or more tables based on a related column:
- **`INNER JOIN`**: Returns records that have matching values in both tables.
- **`LEFT JOIN` (Left Outer Join)**: Returns all records from the left table, and matching records from the right table (with `NULL` if no match).
- **`RIGHT JOIN`**: Returns all records from the right table, and matching records from the left table.
- **`FULL OUTER JOIN`**: Returns all records when there is a match in either left or right table.
