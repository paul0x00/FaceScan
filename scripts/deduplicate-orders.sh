#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_PATH="${1:-$ROOT_DIR/backend/data/db/facescan.sqlite3}"

if [[ ! -f "$DB_PATH" ]]; then
  echo "Database not found: $DB_PATH"
  exit 1
fi

sqlite3 "$DB_PATH" <<'SQL'
BEGIN IMMEDIATE;

WITH ranked AS (
  SELECT
    o.id,
    o.patient_id,
    o.order_no,
    o.created_at,
    (
      SELECT COUNT(*)
      FROM scan_result s
      WHERE s.order_id = o.id
        AND (ifnull(s.ply_path, '') <> '' OR ifnull(s.image_paths, '') <> '' OR ifnull(s.preview_path, '') <> '')
    ) AS data_count,
    (
      SELECT COUNT(*)
      FROM scan_result s
      WHERE s.order_id = o.id
    ) AS scan_count,
    ROW_NUMBER() OVER (
      PARTITION BY o.patient_id, o.order_no
      ORDER BY
        (
          SELECT COUNT(*)
          FROM scan_result s
          WHERE s.order_id = o.id
            AND (ifnull(s.ply_path, '') <> '' OR ifnull(s.image_paths, '') <> '' OR ifnull(s.preview_path, '') <> '')
        ) DESC,
        (
          SELECT COUNT(*)
          FROM scan_result s
          WHERE s.order_id = o.id
        ) DESC,
        o.created_at ASC,
        o.id ASC
    ) AS rn
  FROM orders o
  WHERE ifnull(o.order_no, '') <> ''
),
to_delete AS (
  SELECT id FROM ranked WHERE rn > 1
)
DELETE FROM scan_result WHERE order_id IN (SELECT id FROM to_delete);

WITH ranked AS (
  SELECT
    o.id,
    ROW_NUMBER() OVER (
      PARTITION BY o.patient_id, o.order_no
      ORDER BY
        (
          SELECT COUNT(*)
          FROM scan_result s
          WHERE s.order_id = o.id
            AND (ifnull(s.ply_path, '') <> '' OR ifnull(s.image_paths, '') <> '' OR ifnull(s.preview_path, '') <> '')
        ) DESC,
        (
          SELECT COUNT(*)
          FROM scan_result s
          WHERE s.order_id = o.id
        ) DESC,
        o.created_at ASC,
        o.id ASC
    ) AS rn
  FROM orders o
  WHERE ifnull(o.order_no, '') <> ''
),
to_delete AS (
  SELECT id FROM ranked WHERE rn > 1
)
DELETE FROM orders WHERE id IN (SELECT id FROM to_delete);

COMMIT;
SQL

echo "Duplicate orders cleaned in: $DB_PATH"
