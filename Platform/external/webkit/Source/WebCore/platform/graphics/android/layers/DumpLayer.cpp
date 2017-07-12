/*
 * Copyright 2012, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "DumpLayer.h"

#if USE(ACCELERATED_COMPOSITING)

namespace WebCore {

void lwrite(FILE* file, const char* str)
{
    fwrite(str, sizeof(char), strlen(str), file);
}

void writeIndent(FILE* file, int indentLevel)
{
    if (indentLevel)
        fprintf(file, "%*s", indentLevel*2, " ");
}

void writeln(FILE* file, int indentLevel, const char* str)
{
    writeIndent(file, indentLevel);
    lwrite(file, str);
    lwrite(file, "\n");
}

void writeIntVal(FILE* file, int indentLevel, const char* str, int value)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = %d;\n", str, value);
}

void writeStrVal(FILE* file, int indentLevel, const char* str, const char* value)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = %s;\n", str, value);  
}

void writeHexVal(FILE* file, int indentLevel, const char* str, int value)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = %x;\n", str, value);
}

void writeFloatVal(FILE* file, int indentLevel, const char* str, float value)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = %.3f;\n", str, value);
}

void writePoint(FILE* file, int indentLevel, const char* str, SkPoint point)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = { x = %.3f; y = %.3f; };\n", str, point.fX, point.fY);
}

void writeIntPoint(FILE* file, int indentLevel, const char* str, IntPoint point)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = { x = %d; y = %d; };\n", str, point.x(), point.y());
}

void writeSize(FILE* file, int indentLevel, const char* str, SkSize size)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = { w = %.3f; h = %.3f; };\n", str, size.width(), size.height());
}

void writeRect(FILE* file, int indentLevel, const char* str, SkRect rect)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = { x = %.3f; y = %.3f; w = %.3f; h = %.3f; };\n",
            str, rect.fLeft, rect.fTop, rect.width(), rect.height());
}

void writeMatrix(FILE* file, int indentLevel, const char* str, const TransformationMatrix& matrix)
{
    writeIndent(file, indentLevel);
    fprintf(file, "%s = { (%.2f,%.2f,%.2f,%.2f),(%.2f,%.2f,%.2f,%.2f),"
            "(%.2f,%.2f,%.2f,%.2f),(%.2f,%.2f,%.2f,%.2f) };\n",
            str,
            matrix.m11(), matrix.m12(), matrix.m13(), matrix.m14(),
            matrix.m21(), matrix.m22(), matrix.m23(), matrix.m24(),
            matrix.m31(), matrix.m32(), matrix.m33(), matrix.m34(),
            matrix.m41(), matrix.m42(), matrix.m43(), matrix.m44());
}

} // namespace WebCore

#endif // USE(ACCELERATED_COMPOSITING)
