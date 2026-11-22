/*
 *  Copyright (c) 2013 Croatia Control Ltd. (www.crocontrol.hr)
 *
 *  This file is part of Asterix.
 *
 *  Asterix is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Asterix is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Asterix.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * AUTHORS: Damir Salantic, Croatia Control Ltd.
 *
 */

#include "DataItemFormatRepetitive.h"
#include "Tracer.h"
#include "asterixformat.hxx"
#include "SqlSchema.h"

DataItemFormatRepetitive::DataItemFormatRepetitive(int id)
        : DataItemFormat(id) {
}

DataItemFormatRepetitive::DataItemFormatRepetitive(const DataItemFormatRepetitive &obj)
        : DataItemFormat(obj.m_nID) {
    std::list<DataItemFormat *>::iterator it = ((DataItemFormat &) obj).m_lSubItems.begin();

    while (it != obj.m_lSubItems.end()) {
        DataItemFormat *di = (DataItemFormat *) (*it);
        m_lSubItems.push_back(di->clone());
        it++;
    }

    m_pParentFormat = obj.m_pParentFormat;
}

DataItemFormatRepetitive::~DataItemFormatRepetitive() {
}

long DataItemFormatRepetitive::getLength(const unsigned char *pData, long nMaxLength) {
    DataItemFormat *pF = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pF == NULL) {
        Tracer::Error("Wrong data in Repetitive");
        return 0;
    }
    if (nMaxLength < 1) { // the repetition factor itself is missing
        return nMaxLength + 1;
    }
    unsigned char nRepetition = *pData;
    return (1 + nRepetition * pF->getLength(pData + 1, nMaxLength - 1));
}

bool DataItemFormatRepetitive::getText(std::string &strResult, std::string &strHeader, const unsigned int formatType,
                                       unsigned char *pData, long nLength) {
    bool ret = false;
    DataItemFormat *pF = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pF == NULL) {
        Tracer::Error("Wrong data in Repetitive");
        return true;
    }

    if (nLength < 1) {
        Tracer::Error("Wrong length in Repetitive");
        return true;
    }
    int fixedLength = pF->getLength(pData + 1, nLength - 1);
    unsigned char nRepetition = *pData;

    if (1 + nRepetition * fixedLength != nLength) {
        Tracer::Error("Wrong length in Repetitive");
        return true;
    }

    pData++;

    switch (formatType) {
        case CAsterixFormat::EJSON:
        case CAsterixFormat::EJSONX:
        case CAsterixFormat::EJSONH:
        case CAsterixFormat::EJSONE: {
            std::string tmpStr = format("[");
            if (nRepetition == 0) {
                ret = true;
            } else {
                // a repetition that prints nothing (with -LF, a BDS register not in the filter) is left out,
                // with its separator: the list stays valid JSON
                while (nRepetition--) {
                    const std::string::size_type elementStart = tmpStr.length();
                    if (ret)
                        tmpStr += ',';
                    if (pF->getText(tmpStr, strHeader, formatType, pData, fixedLength))
                        ret = true;
                    else
                        tmpStr.resize(elementStart);
                    pData += fixedLength;
                }
            }
            tmpStr += format("]");

            if (ret)
                strResult += tmpStr;

            break;
        }
        case CAsterixFormat::ESQL: {
            // each repetition is a row of the child table (a BDS register opens the row of its own table)
            SqlRecord *pRecord = SqlRecord::s_pCurrent;
            bool bds = pF->isBDS();
            if (nRepetition == 0) {
                ret = true;
            }
            // rep_idx is the index in the -j list, which leaves out the repetitions that print nothing
            int index = 0;
            for (int i = 0; i < nRepetition; i++) {
                int previous = 0;
                if (pRecord != NULL) {
                    if (bds)
                        pRecord->setNextRepIdx(index);
                    else
                        previous = pRecord->openRow(&m_strSqlTable, index, false);
                }
                if (pF->getText(strResult, strHeader, formatType, pData, fixedLength)) {
                    ret = true;
                    index++;
                }
                if (pRecord != NULL && !bds)
                    pRecord->closeRow(previous);
                pData += fixedLength;
            }
            break;
        }
        default: {
            if (nRepetition == 0) {
                ret = true;
            } else {
                while (nRepetition--) {
                    ret |= pF->getText(strResult, strHeader, formatType, pData, fixedLength);
                    pData += fixedLength;
                }   
            }
            break;
        }
    }
    return ret;
}

std::string DataItemFormatRepetitive::printDescriptors(std::string header) {
    DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pFixed == NULL) {
        Tracer::Error("Wrong data in Repetitive");
        return "Wrong data in Repetitive";
    }

    return pFixed->printDescriptors(header);
}

void DataItemFormatRepetitive::addSqlColumns(SqlSchema &schema, int table, const std::string &prefix) {
    DataItemFormat *pF = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pF == NULL)
        return;
    if (pF->isBDS()) { // a child table per BDS register
        pF->addSqlColumns(schema, table, prefix);
        return;
    }
    int child = schema.addChildTable(table, SqlSchema::tableSuffix(prefix), false);
    m_strSqlTable = schema.tableName(child);
    pF->addSqlColumns(schema, child, "");
}

bool DataItemFormatRepetitive::filterOutItem(const char *name) {
    DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pFixed == NULL) {
        Tracer::Error("Wrong data in Repetitive");
        return false;
    }

    return pFixed->filterOutItem(name);
}

bool DataItemFormatRepetitive::isFiltered(const char *name) {
    DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (pFixed == NULL) {
        Tracer::Error("Wrong data in Repetitive");
        return false;
    }

    return pFixed->isFiltered(name);
}

const char *DataItemFormatRepetitive::getDescription(const char *field, const char *value = NULL) {
    std::list<DataItemFormat *>::iterator it;
    for (it = m_lSubItems.begin(); it != m_lSubItems.end(); it++) {
        DataItemBits *bv = (DataItemBits *) (*it);
        const char *desc = bv->getDescription(field, value);
        if (desc != NULL)
            return desc;
    }
    return NULL;
}

#if defined(WIRESHARK_WRAPPER) || defined(ETHEREAL_WRAPPER)
fulliautomatix_definitions* DataItemFormatRepetitive::getWiresharkDefinitions()
{
    DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (!pFixed)
    {
        Tracer::Error("Wring format of repetitive item");
        return NULL;
    }
    return pFixed->getWiresharkDefinitions();
}

fulliautomatix_data* DataItemFormatRepetitive::getData(unsigned char* pData, long len, int byteoffset)
{
    fulliautomatix_data *lastData = NULL, *firstData = NULL;
    DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
    if (!pFixed)
    {
        Tracer::Error("Wrong format of repetitive item");
        return NULL;
    }

    if (len < 1)
    {
        Tracer::Error("Wrong length in repetitive item");
        return NULL;
    }
    int fixedLength = pFixed->getLength(pData + 1, len - 1);
    unsigned char nRepetition = *pData;

    firstData = lastData = newDataUL(NULL, PID_REP, byteoffset, 1, nRepetition);
    byteoffset+=1;

    if (1+nRepetition*fixedLength != len)
    {
        Tracer::Error("Wrong length in Repetitive");
        return firstData;
    }

    pData++;

    while(nRepetition--)
    {
        lastData->next = pFixed->getData(pData, fixedLength, byteoffset);
        while(lastData->next)
            lastData = lastData->next;

        pData += fixedLength;
        byteoffset += fixedLength;
    }

    return firstData;
}
#endif

#if defined(PYTHON_WRAPPER)

PyObject* DataItemFormatRepetitive::getObject(unsigned char* pData, long nLength, int verbose)
{
    PyObject* p = PyList_New(0);

     DataItemFormat *pFixed = m_lSubItems.size() ? m_lSubItems.front() : NULL; // Fixed or BDS
      if (!pFixed)
      {
        PyObject* p1 = Py_BuildValue("s", "Wrong format of Repetitive item");
        PyList_Append(p, p1);
        Py_DECREF(p1);
        return p;
      }

      int fixedLength = nLength < 1 ? 0 : pFixed->getLength(pData + 1, nLength - 1);
      unsigned char nRepetition = nLength < 1 ? 1 : *pData; // nLength < 1 fails the check below

      if (1+nRepetition*fixedLength != nLength)
      {
        PyObject* p1 = Py_BuildValue("s", "Wrong length in Repetitive item");
        PyList_Append(p, p1);
        Py_DECREF(p1);
        return p;
      }

      pData++;

      while(nRepetition--)
      {
        PyObject* p1 = pFixed->getObject(pData, fixedLength, verbose);
        PyList_Append(p, p1);
        Py_DECREF(p1);
        pData += fixedLength;
      }

    return p;
}


void DataItemFormatRepetitive::insertToDict(PyObject* p, unsigned char* pData, long nLength, int verbose)
{
 // Not supported
}
#endif
