USE plato;

INSERT INTO Output WITH MONOTONIC_KEYS
SELECT
    *
FROM Input1
UNION ALL
SELECT
    *
FROM Input2
ORDER BY
    key,
    subkey;
