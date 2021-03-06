#include "record_mgr.h"
#include "buffer_mgr_stat.h"
#include "dberror.h"
#include "buffer_mgr.h"
#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_HEADER_PAGE   0
#define FIRST_RECORD_PAGE 1

// table and manager
RC initRecordManager (void *mgmtData){
	int ret = 0;

	return ret;
}

RC shutdownRecordManager (){
	int ret = 0;

	return ret;
}

/**************************************************************************
 • description
   Inite table Header which store table informations
 
 • parameters
    char *name, Schema *schema, BM_BufferPool *bm
 
 • return value
    return ret
 ***************************************************************************/
RC initTableHeader(char *name, Schema *schema, BM_BufferPool *bm) {
    int ret, HeaderPageNum = 0, i, RecordSize;
    RM_TableHeader TableHeader;
    unsigned int offset=0 , size = 0;

    BM_PageHandle *page = MAKE_PAGE_HANDLE();

    ret = pinPage(bm, page, HeaderPageNum);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    RecordSize = getRecordSize(schema);

    //fullfile the pageHeader structure
    strcpy(TableHeader.tableName, name);
    TableHeader.numRecorders = 0;
    TableHeader.totalPages = 1;
    TableHeader.totalslot = 0;
    TableHeader.recordersPerPage = (PAGE_SIZE - sizeof(RM_BlockHeader))/RecordSize;
    TableHeader.numSchemaAttr = schema->numAttr;
    TableHeader.key = *(schema->keyAttrs);

    // 1st phase copy the content of Tableheader to frame
    memcpy(page->data, &TableHeader, sizeof(RM_TableHeader));
   
    // copy attr datatype to frame
    offset = (unsigned int)sizeof(RM_TableHeader);
    size = schema->numAttr * sizeof(int);

    memcpy((char *)page->data + offset, schema->dataTypes, size);

    // copy attr size to frame
    offset += size;
    size = schema->numAttr * sizeof(int);

    RecordSize = getRecordSize(schema);
    memcpy((char *)page->data + offset, schema->typeLength, size);
    
    //copy attr name to frame
    for (i = 0; i < schema->numAttr; i ++) {
        offset += size;
        size = sizeof(char);
        memcpy((char *)page->data + offset, *(schema->attrNames + i), size);
    }

    ret = markDirty(bm, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }

    unpinPage(bm, page);
    free(page);

    return ret;
}

/**************************************************************************
 • description
   Create the page which store the record
 
 • parameters
    int PageNum, RM_TableData *rel
 
 • return value
    return ret
 ***************************************************************************/
RC CreateRecordPage(int PageNum, RM_TableData *rel) {
    int ret = 0, RecordSize;
    struct RM_BlockHeader blockheader;

    BM_PageHandle *page = (BM_PageHandle *) malloc(sizeof(BM_PageHandle));

    ret = pinPage(rel->mgmtData, page, PageNum);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    RecordSize = getRecordSize(rel->schema);
    blockheader.blockID = PageNum;
    blockheader.type = Block_EmpPage;
    blockheader.RecordsCapacity = (PAGE_SIZE - sizeof(RM_BlockHeader))/RecordSize;
    blockheader.freeSlot = 0;
    blockheader.numRecords = 0;

    memcpy((char *)page->data , &blockheader, sizeof(RM_BlockHeader));
 
    ret = markDirty(rel->mgmtData, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }

    ret = forcePage(rel->mgmtData, page);
    if (ret != RC_OK) {
        printf("%s Flush Page faiil\n", __func__);
        return RC_BM_BP_FLUSH_PAGE_FAILED;
    }

    //updateTableHeader(PlusOnePageNum);
    unpinPage(rel->mgmtData, page);
    free(page);

    return ret;
}

/**************************************************************************
 • description
   create table which store pages
 
 • parameters
    char *name, Schema *schema
 
 • return value
    return ret
 ***************************************************************************/
RC createTable (char *name, Schema *schema) {
	int ret = 0;

    if(schema == NULL) {
        printf("schema is NULL\n");
        return RC_REC_SCHEMA_NOT_FOUND;
    }
    
    if(name == NULL) {
        printf("File name is NULL\n");
        return RC_REC_FILE_NOT_FOUND;
    }
   
    BM_BufferPool *bm = MAKE_POOL();

    //Create Page File
    ret = createPageFile(name);
    
    if (ret != RC_OK) {
        printf("Create Table Page File %s Fail\n", name);
        return RC_REC_TABLE_CREATE_FAILED;
    }
    
    //Initialize Buffer Pool
    ret = initBufferPool(bm, name, 3, RS_FIFO, NULL);
    if (ret != RC_OK) {
        printf("Init Buffer POOL Fail\n");
        return RC_BM_BP_INIT_BUFFER_POOL_FAILED;
    }
    
    //Initialize the Block Header
    ret = initTableHeader(name, schema, bm);
    if (ret != RC_OK) {
        printf("Init Page Header Fail\n");
        return RC_BM_BP_INIT_BUFFER_POOL_FAILED;
    }

    ret = shutdownBufferPool(bm);
    if (ret != RC_OK) {
        printf("Shutdown Buffer Pool Fail\n");
        return RC_BM_BP_SHUTDOWN_BUFFER_POOL_FAILED;
    }

	return ret;
}

/**************************************************************************
 • description
   parse page header which used to fetch table information
 
 • parameters
    char * page, Schema *schema
 
 • return value
    return ret
 ***************************************************************************/
struct RM_TableHeader * parsePageHeader(char * page, Schema *schema) {
    int ret = 0;
    int numAttr = 0;
    int *cpSizes, *cpKeys,* keyAttrs;
    unsigned int offset;
    struct RM_TableHeader * TableHeader;
    DataType *cpDt;
    char * attrNames;
    
    TableHeader = (struct RM_TableHeader *)malloc(sizeof(RM_TableHeader));

    memcpy(TableHeader, page, sizeof(RM_TableHeader));

    cpDt = (DataType *) malloc(sizeof(DataType) * TableHeader->numSchemaAttr);
    cpSizes = (int *) malloc(sizeof(int) * TableHeader->numSchemaAttr);
    attrNames = (char *) malloc(sizeof(char) * TableHeader->numSchemaAttr);
    keyAttrs = (int *) malloc(sizeof(int));

    numAttr = TableHeader->numSchemaAttr;

    // parse datatype
    offset =  sizeof(RM_TableHeader);
    memcpy(cpDt, page + offset, numAttr * sizeof(int));

    memcpy(keyAttrs, &(TableHeader->key), sizeof(int));

    //parse data type size
    offset += numAttr * sizeof(int);
    memcpy(cpSizes, page + offset, numAttr * sizeof(int));

    offset += numAttr * sizeof(int);
    strncpy(attrNames, page + offset, numAttr * sizeof(char));
    
    //full fill schema structure
    schema->dataTypes = cpDt;
    schema->attrNames = &attrNames;
    schema->typeLength = cpSizes;
    schema->numAttr = numAttr;
    schema->keyAttrs = keyAttrs;
 
    return TableHeader;
}

/**************************************************************************
 • description
   get table information from the disk and set the information to the rel 
 
 • parameters
    RM_TableData *rel, char *name
 
 • return value
    return ret
 ***************************************************************************/
RC openTable (RM_TableData *rel, char *name) {
    int ret = 0;
    Schema *schema;
    struct RM_TableHeader *TableHeader;

    BM_PageHandle *page = MAKE_PAGE_HANDLE();
    BM_BufferPool *bm = MAKE_POOL();

    schema = malloc(sizeof(Schema));
    if (schema == NULL) {
        printf("malloc schema size fail\n");
        return -1;
    }
    
    //Initialize Buffer Pool
    ret = initBufferPool(bm, name, 3, RS_FIFO, NULL);
    if (ret != RC_OK) {
        printf("Init Buffer POOL Fail\n");
        return RC_BM_BP_INIT_BUFFER_POOL_FAILED;
    }


    ret = pinPage(bm, page, TABLE_HEADER_PAGE);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    TableHeader = parsePageHeader(page->data, schema);

    bm->pageFile = name;

    rel->name = name;
    rel->schema = schema;
    rel->mgmtData = bm;
    rel->TableHeader = TableHeader;

    free(page);
    return ret;
}

/**************************************************************************
 • description
   close table and save table information to disk
 
 • parameters
    RM_TableData *rel
 
 • return value
    return ret
 ***************************************************************************/
RC closeTable (RM_TableData *rel){
    int ret = 0;
    BM_BufferPool *bm;

    bm = rel->mgmtData;

    BM_PageHandle *page = MAKE_PAGE_HANDLE();
    
    // Write Table Header information from memory to Disk
    ret = pinPage(rel->mgmtData, page, TABLE_HEADER_PAGE);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }
    // 1st phase copy the content of Tableheader to frame
    memcpy(page->data, rel->TableHeader, sizeof(RM_TableHeader));
    
    ret = markDirty(rel->mgmtData, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }

    ret = forcePage(bm, page);
    if (ret != RC_OK) {
        printf("%s Flush Page faiil\n", __func__);
        return RC_BM_BP_FLUSH_PAGE_FAILED;
    }

    free(rel->schema);
    free(rel->TableHeader);
    unpinPage(bm, page);

    free(page);

    return ret;
}

/**************************************************************************
 • description
   delete the table and destroy pages files
 
 • parameters
    char *name
 
 • return value
    return ret
 ***************************************************************************/
RC deleteTable (char *name){
    int ret = 0;

    ret = destroyPageFile(name);
    if(ret != RC_OK) {
        printf("Detele Table Fail\n");
        return ret;
    }

    return ret;
}

/**************************************************************************
 • description
   getNum total Tuples fromt he table header
 
 • parameters
    RM_TableData *rel
 
 • return value
    return totalslot
 ***************************************************************************/
int getNumTuples (RM_TableData *rel) {
    int ret = 0;
    struct RM_TableHeader *TableHeader;
    
    TableHeader = rel->TableHeader;

    return TableHeader->totalslot;
}

/**************************************************************************
 • description
   insert Record at the end of the file 
 
 • parameters
    RM_TableData *rel, Record *record
 
 • return value
    return ret
 ***************************************************************************/
RC insertRecord (RM_TableData *rel, Record *record) {
    int ret = 0, avaliablePageNum = 0, newPageNum, offset, RecordSize;
    struct RM_TableHeader * tableHeader;
    struct RM_BlockHeader blockHeader;
    BM_BufferPool *bm;

    bm = rel->mgmtData;

    BM_PageHandle *page = MAKE_PAGE_HANDLE();
    
    tableHeader = rel->TableHeader;
    newPageNum = tableHeader->totalPages;
    RecordSize = getRecordSize(rel->schema);

    blockHeader.type = Block_Used;
    
    // No Free slot to insert record
    if (tableHeader->numRecorders == tableHeader->totalslot) {
        ret = CreateRecordPage(newPageNum, rel);
        tableHeader->totalPages += 1;
        tableHeader->totalslot += tableHeader->recordersPerPage;
    }

    ret = pinPage(bm, page, tableHeader->totalPages - 1);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    //Get the last Page Header that used to insert recorder in it.
    memcpy(&blockHeader, page->data, sizeof(RM_BlockHeader));
    
    offset = sizeof(RM_BlockHeader) + (blockHeader.freeSlot) * RecordSize;
    memcpy(page->data + offset, record->data, RecordSize);
    
    record->id.page = tableHeader->totalPages - 1;
    record->id.slot = blockHeader.freeSlot;
    
    blockHeader.freeSlot +=1;
    blockHeader.numRecords +=1;
    
    if (blockHeader.numRecords == blockHeader.RecordsCapacity)
        blockHeader.type = Block_Full;
    else
        blockHeader.type = Block_Used;
    
    //update page header
    memcpy(page->data, &blockHeader, sizeof(RM_BlockHeader));

    tableHeader->numRecorders += 1;

    ret = markDirty(rel->mgmtData, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }

    ret = forcePage(bm, page);
    if (ret != RC_OK) {
        printf("%s Flush Page faiil\n", __func__);
        return RC_BM_BP_FLUSH_PAGE_FAILED;
    }

    unpinPage(bm, page);
    free(page);

    return ret;
}

/**************************************************************************
 • description
   delete record by set the record to ############
 
 • parameters
    RM_TableData *rel, RID id
 
 • return value
    return ret
 ***************************************************************************/
RC deleteRecord (RM_TableData *rel, RID id) {
    int ret = 0, offset= 0;
    struct RM_TableHeader *tableHeader;
    struct RM_BlockHeader blockHeader;

    tableHeader = rel->TableHeader;

    if (id.page > tableHeader->totalPages-1) {
        printf("Page ID[0-N] %d is illegal.Total have pages %d\n", id.page, tableHeader->totalPages);
        return RC_REC_PIN_PAGE_NON_EXIT;
    }

    BM_PageHandle *page = (BM_PageHandle *)malloc(sizeof(BM_PageHandle));
    
    ret = pinPage(rel->mgmtData, page, id.page);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    memcpy(&blockHeader, page->data, sizeof(RM_BlockHeader));

    if (id.slot > blockHeader.freeSlot) {
        printf("Slot is illegal\n");
        return RC_INVALID_SLOT_NUMBER;
    }

    blockHeader.numRecords -=1;
    memcpy(page->data, &blockHeader, sizeof(RM_BlockHeader));

    offset = sizeof(RM_BlockHeader) + id.slot * getRecordSize(rel->schema);
    memset(page->data + offset, '#', getRecordSize(rel->schema));

    tableHeader->numRecorders -=1;
    tableHeader->totalslot -=1;

    ret = markDirty(rel->mgmtData, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }
    
    ret = forcePage(rel->mgmtData, page);
    if (ret != RC_OK) {
        printf("%s Flush Page faiil\n", __func__);
        return RC_BM_BP_FLUSH_PAGE_FAILED;
    }

    unpinPage(rel->mgmtData, page);
    free(page);

    return ret;
}

/**************************************************************************
 • description
   update Record data for order slot
 
 • parameters
    RM_TableData *rel, Record *record
 
 • return value
    return ret
 ***************************************************************************/
RC updateRecord (RM_TableData *rel, Record *record) {
    int ret = 0, offset;
    
    BM_PageHandle *page = (BM_PageHandle *)malloc(sizeof(BM_PageHandle));

    ret = pinPage(rel->mgmtData, page, record->id.page);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }

    offset = sizeof(RM_BlockHeader) + record->id.slot * getRecordSize(rel->schema);
    memcpy(page->data + offset, record->data, getRecordSize(rel->schema));
    
    ret = markDirty(rel->mgmtData, page);
    if (ret != RC_OK){
        printf("%s Mark Dirty Page fail\n", __func__);
        return ret;
    }

    ret = forcePage(rel->mgmtData, page);
    if (ret != RC_OK) {
        printf("%s Flush Page faiil\n", __func__);
        return RC_BM_BP_FLUSH_PAGE_FAILED;
    }

    unpinPage(rel->mgmtData, page);
    free(page);
    
    return ret;
}

/**************************************************************************
 • description
   get record by the order slot
 
 • parameters
    RM_TableData *rel, RID id, Record *record
 
 • return value
    return ret
 ***************************************************************************/
RC getRecord (RM_TableData *rel, RID id, Record *record) {
    int ret = 0, offset, recordsize;
    struct RM_TableHeader *tableHeader;
    struct RM_BlockHeader blockHeader;
    char *tomb;

    tableHeader = rel->TableHeader;
    recordsize = getRecordSize(rel->schema);
    
    tomb = (char *)malloc(recordsize);
    memset(tomb, '#', recordsize);

    if (id.page > tableHeader->totalPages-1) {
        printf("Page ID[0-N] %d is illegal.Total have pages %d\n", id.page, tableHeader->totalPages);
        return RC_REC_PIN_PAGE_NON_EXIT;
    }
    
    BM_PageHandle *page = (BM_PageHandle *)malloc(sizeof(BM_PageHandle));
    record->id = id;

    ret = pinPage(rel->mgmtData, page, id.page);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }
    
    memcpy(&blockHeader, page->data, sizeof(RM_BlockHeader));
    
    if (id.slot > blockHeader.freeSlot) {
        printf("%s lot num %dis illegal\n", __func__, id.slot);
        return RC_INVALID_SLOT_NUMBER;
    }

    offset = sizeof(RM_BlockHeader) + id.slot * recordsize;

    memcpy(record->data, page->data + offset, recordsize);

    if (0 == strncmp(record->data, tomb, recordsize)) {
        printf("%s, %d page %d slot %d is stome\n",__func__,__LINE__, id.page, id.slot);
        ret =  RC_RECORD_REMOVED;
    }

    unpinPage(rel->mgmtData, page);
    
    free(page);
    return ret;
}

/**************************************************************************
 • description
   fullfill the scanhelper structure which used for scan table 
 
 • parameters
   RM_TableData *rel, RM_ScanHandle *scan, Expr *cond
 
 • return value
    return ret
 ***************************************************************************/
// scans
RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond) {
    int ret = 0;
    RM_ScanHelper * helper;
    
    helper= (RM_ScanHelper *)malloc(sizeof(RM_ScanHelper));

    helper->conditon = cond;
    helper->rid.page = 1;
    helper->rid.slot = 0;
    helper->stopSign = ((RM_TableHeader *)rel->TableHeader)->numRecorders;
    helper->current = 0;
    
    scan->rel = rel;
    scan->mgmtData = helper;

    return ret;
}

/**************************************************************************
 • description
   get the next record by the ordered slot id and page id 
 
 • parameters
   RM_ScanHandle *scan, Record *record
 
 • return value
    return ret
 ***************************************************************************/
RC next (RM_ScanHandle *scan, Record *record) {
	int ret = 0;
    int page_curr,slot, curr, total;
    RM_TableData *table;
    RM_ScanHelper *helper;
    
    RID *id = malloc(sizeof(RID));
    
    table = scan->rel;
    helper = scan->mgmtData;
    
    page_curr = helper->rid.page;
    slot = helper->rid.slot;
    curr = helper->current =slot;
    total = helper->stopSign;

    BM_BufferPool *bm = (BM_BufferPool *)table->mgmtData;
    BM_PageHandle *page = (BM_PageHandle *)malloc(sizeof(BM_PageHandle));

    ret = pinPage(bm, page, page_curr);
    if (ret != RC_OK){
        printf("%s pin page fail\n", __func__);
        return ret;
    }
    
    RM_BlockHeader blockHeader;
    memcpy(&blockHeader, page->data, sizeof(RM_BlockHeader));
    if(slot >blockHeader.freeSlot) {
        slot = 0;
        page_curr = page_curr+1;
        if(page_curr>((RM_TableHeader *)table->TableHeader)->totalPages-1) {
            return RC_RM_NO_MORE_TUPLES;
        }
        unpinPage(bm, page);
        pinPage(bm, page, page_curr);
    }
    id->page = page_curr;
    id->slot = slot;
    
    if(curr == total) {
        return RC_RM_NO_MORE_TUPLES;  //RC_RM_NO_MORE_TUPLES
    }
    
    Record *result;
    ret=createRecord(&result, table->schema);
    Value *v = malloc(sizeof(Value));
    ret = getRecord(table, *id, result);

    
    unpinPage(bm, page);
    //free(page);
    if (ret == RC_RECORD_REMOVED) {
        helper->rid.slot++;
        curr++;
        return next(scan,record);
    } else {
        evalExpr(result, table->schema, helper->conditon, &v);
        if(v->v.boolV==1) {
            record->id = result->id;
            record->data = result->data;
            helper->rid.slot++;
            curr++;
            //return next(scan,record);
            return RC_OK;
        } else {
            helper->rid.slot++;
            curr++;
            return next(scan,record);
        }
    }
    
    return ret;
}

/**************************************************************************
 • description
   close the scan structure
 
 • parameters
   RM_ScanHandle *scan
 
 • return value
    return ret
 ***************************************************************************/
RC closeScan (RM_ScanHandle *scan) {
    int ret = 0;
    scan->mgmtData = NULL;
    scan->rel = NULL;
    return ret;
}

/**************************************************************************
 • description
   get the record size
 
 • parameters
   Schema *schema
 
 • return value
    return ret
 ***************************************************************************/
int getRecordSize (Schema *schema) {
    int recordSize = 0, i;
    
    for (i = 0; i < schema->numAttr; i++) {
        switch(schema->dataTypes[i]){
            case DT_INT:
                schema->typeLength[i] = sizeof(int);
                break;
            case DT_FLOAT:
                schema->typeLength[i] = sizeof(float);
                break;
            case DT_BOOL:
                schema->typeLength[i] = sizeof(bool);
                break;
        }

        recordSize += schema->typeLength[i];
    }
   return recordSize;
}

/**************************************************************************
 • description
   create the schema and fullfill the struct of schema
 
 • parameters
   int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys
 
 • return value
    return schema
 ***************************************************************************/
Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys) {
    int ret = 0;
    Schema *schema;

    schema = malloc(sizeof(Schema));
    if (schema == NULL) {
        printf("malloc schema size fail\n");
        return NULL;
    }
 
    schema->numAttr = numAttr;
    schema->attrNames = attrNames;
    schema->dataTypes = dataTypes;
    schema->typeLength = typeLength;
    schema->keyAttrs = keys;
    schema->keySize = keySize;

    return schema;
}

/**************************************************************************
 • description
   free the data memory which store schema information
 
 • parameters
   Schema *schema
 
 • return value
    return ret
 ***************************************************************************/
RC freeSchema (Schema *schema){
    int ret = 0, i;

    for (i=0; i<schema->numAttr; i++)
        free(schema->attrNames[i]);
    
    free(schema);

    return ret;
}

/**************************************************************************
 • description
   create Record which malloc memory for the record
 
 • parameters
   Record **record, Schema *schema

 • return value
    return ret
 ***************************************************************************/
RC createRecord (Record **record, Schema *schema) {
    int ret = 0;
    int RecordSize;

    *record = malloc(sizeof(Record));

    RecordSize = getRecordSize(schema);
   
    (*record)->id = *(RID *)malloc(sizeof(RID));

    (*record)->data = (char *) calloc(RecordSize, sizeof(char));

    return ret;
}

/**************************************************************************
 • description
   free the record memory space
 
 • parameters
   Record *record

 • return value
    return ret
 ***************************************************************************/
RC freeRecord (Record *record) {
    int ret = 0;
    
    free(record->data);
    free(record);

    return ret;
}

/**************************************************************************
 • description
   get the attribute data with ordered attribute id.
 
 • parameters
   Record *record, Schema *schema, int attrNum, Value **value

 • return value
    return ret
 ***************************************************************************/
RC getAttr (Record *record, Schema *schema, int attrNum, Value **value) {
    int ret = 0, offset = 0, i;
    Value *Val;

    Val = (Value *) malloc(sizeof(Value));
    
    Val->dt = schema->dataTypes[attrNum];
    
    for (i = 0; i < attrNum; i++) {
        offset += schema->typeLength[i];
    }

    switch(schema->dataTypes[attrNum]) {
        case DT_STRING:
            Val->v.stringV = (char *) malloc(schema->typeLength[attrNum] + 1);
            memcpy(Val->v.stringV, record->data + offset,schema->typeLength[attrNum]);
            Val->v.stringV[schema->typeLength[attrNum]] = '\0';
            break;
        case DT_INT:
            memcpy(&(Val->v.intV), record->data + offset,schema->typeLength[attrNum]);
            break;
        case DT_FLOAT:
            memcpy(&(Val->v.floatV), record->data + offset,schema->typeLength[attrNum]);
            break;
        case DT_BOOL:
            memcpy(&(Val->v.boolV), record->data + offset,schema->typeLength[attrNum]);
            break;
    }

    *value = Val;

    return ret;
}

/**************************************************************************
 • description
   set the attribute data to ordered attribute id.
 
 • parameters
   Record *record, Schema *schema, int attrNum, Value *value

 • return value
    return ret
 ***************************************************************************/
RC setAttr (Record *record, Schema *schema, int attrNum, Value *value) {
    int ret = 0,  offset = 0, i;

    for (i = 0; i < attrNum; i++) {
        offset += schema->typeLength[i];
    }

    switch(value->dt) {
        case DT_STRING:
            memcpy(record->data + offset, value->v.stringV,schema->typeLength[attrNum]);
            break;
        case DT_BOOL:
            memcpy(record->data + offset, &(value->v.boolV),schema->typeLength[attrNum]);
            break;
        case DT_INT:
            memcpy(record->data + offset, &(value->v.intV),schema->typeLength[attrNum]);
            break;
        case DT_FLOAT:
            memcpy(record->data + offset, &(value->v.floatV),schema->typeLength[attrNum]);
            break;
    }

    return ret;
}
