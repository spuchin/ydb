/* postgres can not */
/* multirun can not */
-- kikimr only: pragma kikimr.UnwrapReadTableValues = "false"; create table plato.Output (key varchar null, subkey varchar null, value varchar null, primary key (key)); commit;
INSERT INTO plato.Output WITH truncate (
    key,
    subkey,
    value
)
SELECT
    key,
    subkey,
    value
FROM plato.Input;
COMMIT;

SELECT
    coalesce(key, ""),
    coalesce(subkey, ""),
    coalesce(value, "")
FROM plato.Output;
