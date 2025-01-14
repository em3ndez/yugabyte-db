---
title: BOOLEAN data types
headerTitle: BOOLEAN data types
linkTitle: Boolean
description: Use the BOOLEAN data type to represent three different states - TRUE, FALSE, or NULL.
menu:
  preview:
    identifier: api-ysql-datatypes-bool
    parent: api-ysql-datatypes
aliases:
  - /preview/api/ysql/datatypes/type_bool
isTocNested: true
showAsideToc: true
---

## Synopsis

The `BOOLEAN` data type represents three different states: `TRUE`, `FALSE`, or `NULL`.

## Description

```
type_specification ::= { BOOLEAN | BOOL }
literal ::= { TRUE | true | 't' | 'y' | 'yes' | 'on' | 1 |
              FALSE | false | 'f' | 'n' | 'no' | 'off' | 0 }
```
