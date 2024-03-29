#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#define MemorySet memset
#define MemoryCopy memcpy
#define CalculateCStringLength strlen
#define CStringToInt atoi

#define Log(...) { fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); }

static int
CharIsAlpha(int c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static int
CharIsDigit(int c)
{
    return (c >= '0' && c <= '9');
}

static int
StringMatchCaseSensitiveN(char *a, char *b, int n)
{
    int matches = 0;
    if(a && b && n)
    {
        matches = 1;
        for(int i = 0; i < n; ++i)
        {
            if(a[i] != b[i])
            {
                matches = 0;
                break;
            }
        }
    }
    return matches;
}

static int
StringMatchCaseInsensitive(char *a, char *b)
{
    int matches = 0;
    if(a && b)
    {
        matches = 1;
        for(int i = 0;; ++i)
        {
            if(a[i] != b[i])
            {
                matches = 0;
                break;
            }
            else if(!a[i])
            {
                break;
            }
        }
    }
    return matches;
}

#define OUTPUT_HTML      (1<<0)
#define OUTPUT_MARKDOWN  (1<<1)
#define OUTPUT_BBCODE    (1<<2)

enum
{
    PAGE_NODE_TYPE_invalid,
    PAGE_NODE_TYPE_title,
    PAGE_NODE_TYPE_sub_title,
    PAGE_NODE_TYPE_description,
    PAGE_NODE_TYPE_text,
    PAGE_NODE_TYPE_paragraph_break,
    PAGE_NODE_TYPE_unordered_list,
    PAGE_NODE_TYPE_ordered_list,
    PAGE_NODE_TYPE_code,
    PAGE_NODE_TYPE_youtube,
    PAGE_NODE_TYPE_image,
    PAGE_NODE_TYPE_link,
    PAGE_NODE_TYPE_feature_button,
    PAGE_NODE_TYPE_lister,
    PAGE_NODE_TYPE_date,
};

enum
{
    CODE_LANGUAGE_none,
    CODE_LANGUAGE_c,
};

enum
{
    ORDERED_LIST_STYLE_numeric,
    ORDERED_LIST_STYLE_alphabetic,
    ORDERED_LIST_STYLE_roman_numeral,
};

#define TEXT_STYLE_BOLD       0x01
#define TEXT_STYLE_ITALICS    0x02
#define TEXT_STYLE_UNDERLINE  0x04
#define TEXT_STYLE_MONOSPACE  0x08

typedef struct PageNode PageNode;
typedef struct PageNode
{
    int type;
    char *string;
    int string_length;
    PageNode *next;
    int text_style_flags;
    
    union
    {
        
        struct UnorderedList
        {
            PageNode *first_item;
        }
        unordered_list;
        
        struct OrderedList
        {
            int order_style;
            PageNode *first_item;
        }
        ordered_list;
        
        struct Code
        {
            int language;
        }
        code;
        
        struct Link
        {
            char *url;
            int url_length;
        }
        link;
        
        struct FeatureButton
        {
            char *image_path;
            int image_path_length;
            char *link;
            int link_length;
        }
        feature_button;
        
        struct Date
        {
            int year;
            int month;
            int day;
        }
        date;
        
    };
}
PageNode;

typedef struct Tokenizer
{
    char *at;
    int line;
    char *file;
}
Tokenizer;

#define PARSE_CONTEXT_MEMORY_BLOCK_SIZE_DEFAULT 4096
typedef struct ParseContextMemoryBlock ParseContextMemoryBlock;
struct ParseContextMemoryBlock
{
    int size;
    int alloc_position;
    void *memory;
    ParseContextMemoryBlock *next;
};

typedef struct ParseError ParseError;
struct ParseError
{
    char *file;
    int line;
    char *message;
};

typedef struct ParseContext
{
    ParseContextMemoryBlock *head;
    ParseContextMemoryBlock *active;
    int error_stack_size;
    int error_stack_size_max;
    ParseError *error_stack;
}
ParseContext;

static void *
ParseContextAllocateMemory(ParseContext *context, int size)
{
    void *memory = 0;
    
    ParseContextMemoryBlock *chunk = context->active;
    if(!chunk || chunk->alloc_position + size > chunk->size)
    {
        ParseContextMemoryBlock *old_chunk = chunk;
        int needed_bytes = size < PARSE_CONTEXT_MEMORY_BLOCK_SIZE_DEFAULT ? PARSE_CONTEXT_MEMORY_BLOCK_SIZE_DEFAULT : size;
        chunk = malloc(sizeof(ParseContextMemoryBlock) + needed_bytes);
        chunk->memory = (char *)chunk + sizeof(ParseContextMemoryBlock);
        chunk->size = needed_bytes;
        chunk->alloc_position = 0;
        chunk->next = 0;
        if(old_chunk)
        {
            old_chunk->next = chunk;
        }
        else
        {
            context->head = chunk;
        }
        context->active = chunk;
    }
    
    memory = (char *)chunk->memory + chunk->alloc_position;
    chunk->alloc_position += size;
    return memory;
}

static char *
ParseContextAllocateCStringCopy(ParseContext *context, char *str)
{
    int needed_bytes = CalculateCStringLength(str)+1;
    char *str_copy = ParseContextAllocateMemory(context, needed_bytes);
    MemoryCopy(str_copy, str, needed_bytes);
    return str_copy;
}

static char *
ParseContextAllocateCStringCopyN(ParseContext *context, char *str, int n)
{
    int needed_bytes = n+1;
    char *str_copy = ParseContextAllocateMemory(context, needed_bytes);
    MemoryCopy(str_copy, str, needed_bytes);
    str_copy[n] = 0;
    return str_copy;
}

static PageNode *
ParseContextAllocateNode(ParseContext *context)
{
    PageNode *node = ParseContextAllocateMemory(context, sizeof(PageNode));
    MemorySet(node, 0, sizeof(*node));
    return node;
}

static void
PushParseError(ParseContext *context, Tokenizer *tokenizer, char *format, ...)
{
    if(!context->error_stack)
    {
        context->error_stack_size = 0;
        context->error_stack_size_max = 32;
        context->error_stack = ParseContextAllocateMemory(context, sizeof(ParseError)*context->error_stack_size_max);
    }
    
    if(context->error_stack_size < context->error_stack_size_max)
    {
        va_list args;
        va_start(args, format);
        int needed_bytes = vsnprintf(0, 0, format, args)+1;
        va_end(args);
        
        char *message = ParseContextAllocateMemory(context, needed_bytes);
        
        va_start(args, format);
        vsnprintf(message, needed_bytes, format, args);
        va_end(args);
        
        message[needed_bytes-1] = 0;
        
        ParseError error = {0};
        {
            error.file = tokenizer->file;
            error.line = tokenizer->line;
            error.message = message;
        }
        
        context->error_stack[context->error_stack_size++] = error;
    }
}

typedef enum TokenType
{
    TOKEN_none,
    TOKEN_text,
    TOKEN_double_newline,
    TOKEN_symbol,
    TOKEN_tag,
}
TokenType;

typedef struct Token
{
    TokenType type;
    char *string;
    int string_length;
}
Token;

static int
CharIsSpace(int c)
{
    return (c <= 32);
}

static int
CharIsSymbol(int c)
{
    return (c == '{' || c == '}' || c == '*' || c == '_' || c == '`');
}

static int
CharIsText(int c)
{
    return !CharIsSymbol(c) && c != '@';
}

static Token
GetNextTokenFromBuffer(char *buffer)
{
    Token token = {0};
    
    for(int i = 0; buffer[i]; ++i)
    {
        // NOTE(rjf): Newline
        if(buffer[i] == '\n' && buffer[i+1] == '\n')
        {
            token.type = TOKEN_double_newline;
            token.string = buffer+i;
            token.string_length = 2;
            break;
        }
        else if(!CharIsSpace(buffer[i]))
        {
            int j = 0;
            
            // NOTE(rjf): Tag
            if(buffer[i] == '@')
            {
                j = i+1;
                for(j=i+1; buffer[j] && !CharIsSpace(buffer[j]); ++j);
                token.type = TOKEN_tag;
            }
            
            // NOTE(rjf): Symbol
            else if(CharIsSymbol(buffer[i]))
            {
                static char *symbolic_blocks_to_break_out[] = {
                    "*",
                    "_",
                    "`",
                    "{",
                    "}",
                };
                
                for(j=i+1; buffer[j] && CharIsSymbol(buffer[j]); ++j);
                token.type = TOKEN_symbol;
                
                for(int k = 0; k < sizeof(symbolic_blocks_to_break_out)/sizeof(symbolic_blocks_to_break_out[0]);
                    ++k)
                {
                    int length_to_compare = CalculateCStringLength(symbolic_blocks_to_break_out[k]);
                    if(StringMatchCaseSensitiveN(symbolic_blocks_to_break_out[k], buffer+i, length_to_compare))
                    {
                        j = i+length_to_compare;
                        break;
                    }
                }
            }
            
            // NOTE(rjf): Text
            else
            {
                for(j=i+1; buffer[j] && CharIsText(buffer[j]) && buffer[j] != '\n'; ++j);
                token.type = TOKEN_text;
                
                // NOTE(rjf): Add skipped whitespace to text node
                for(; i > 0 && CharIsSpace(buffer[i-1]); --i);
            }
            
            if(j != 0)
            {
                token.string = buffer+i;
                token.string_length = j-i;
                break;
            }
        }
    }
    
    return token;
}

static Token
PeekToken(Tokenizer *tokenizer)
{
    Token token = GetNextTokenFromBuffer(tokenizer->at);
    return token;
}

static Token
NextToken(Tokenizer *tokenizer)
{
    Token token = GetNextTokenFromBuffer(tokenizer->at);
    tokenizer->at = token.string + token.string_length;
    return token;
}

static int
RequireTokenType(Tokenizer *tokenizer, TokenType type, Token *token_ptr)
{
    int match = 0;
    Token token = GetNextTokenFromBuffer(tokenizer->at);
    if(token.type == type)
    {
        match = 1;
        if(token_ptr)
        {
            *token_ptr = token;
        }
        tokenizer->at = token.string + token.string_length;
    }
    return match;
}

static int
TokenMatch(Token token, char *string)
{
    return (token.type != TOKEN_none &&
            StringMatchCaseSensitiveN(token.string, string, token.string_length) &&
            string[token.string_length] == 0);
}

static int
RequireToken(Tokenizer *tokenizer, char *string, Token *token_ptr)
{
    int match = 0;
    Token token = GetNextTokenFromBuffer(tokenizer->at);
    if(TokenMatch(token, string))
    {
        match = 1;
        if(token_ptr)
        {
            *token_ptr = token;
        }
        tokenizer->at = token.string + token.string_length;
    }
    return match;
}

static PageNode *
ParseText(ParseContext *context, Tokenizer *tokenizer)
{
    PageNode *result = 0;
    
    Token token = PeekToken(tokenizer);
    int text_style_flags = 0;
    
    PageNode **node_store_target = &result;
    
    while(token.type != TOKEN_none)
    {
        Token tag = {0};
        Token symbol = {0};
        Token text = {0};
        
        if(RequireTokenType(tokenizer, TOKEN_tag, &tag))
        {
            
            if(TokenMatch(tag, "@Title"))
            {
                Token title_text = {0};
                if(RequireToken(tokenizer, "{", 0) &&
                   RequireTokenType(tokenizer, TOKEN_text, &title_text) &&
                   RequireToken(tokenizer, "}", 0))
                {
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_title;
                    node->string = title_text.string;
                    node->string_length = title_text.string_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                }
                else
                {
                    PushParseError(context, tokenizer, "A title tag expects {<title text>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@SubTitle"))
            {
                Token title_text = {0};
                if(RequireToken(tokenizer, "{", 0) &&
                   RequireTokenType(tokenizer, TOKEN_text, &title_text) &&
                   RequireToken(tokenizer, "}", 0))
                {
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_sub_title;
                    node->string = title_text.string;
                    node->string_length = title_text.string_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                }
                else
                {
                    PushParseError(context, tokenizer, "A sub-title tag expects {<subtitle text>} to follow.");
                }
            }

            else if(TokenMatch(tag, "@Description"))
            {
                Token description_text = {0};
                if(RequireToken(tokenizer, "{", 0) &&
                   RequireTokenType(tokenizer, TOKEN_text, &description_text) &&
                   RequireToken(tokenizer, "}", 0))
                {
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_description;
                    node->string = description_text.string;
                    node->string_length = description_text.string_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                }
                else
                {
                    PushParseError(context, tokenizer, "A description tag expects {<description text>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@YouTube"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    char *link = open_bracket.string+1;
                    int link_length = 0;
                    
                    int bracket_stack = 1;
                    for(int i = 0; link[i]; ++i)
                    {
                        if(link[i] == '{')
                        {
                            ++bracket_stack;
                        }
                        else if(link[i] == '}')
                        {
                            --bracket_stack;
                        }
                        
                        if(bracket_stack == 0)
                        {
                            break;
                        }
                        
                        ++link_length;
                    }
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_youtube;
                    node->string = link;
                    node->string_length = link_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = link + link_length;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow YouTube link.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A YouTube tag expects {<link>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@Image"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    char *link = open_bracket.string+1;
                    int link_length = 0;
                    
                    int bracket_stack = 1;
                    for(int i = 0; link[i]; ++i)
                    {
                        if(link[i] == '{')
                        {
                            ++bracket_stack;
                        }
                        else if(link[i] == '}')
                        {
                            --bracket_stack;
                        }
                        
                        if(bracket_stack == 0)
                        {
                            break;
                        }
                        
                        ++link_length;
                    }
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_image;
                    node->string = link;
                    node->string_length = link_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = link + link_length;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow image path.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A YouTube tag expects {<link>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@Code"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    char *link = open_bracket.string+1;
                    int link_length = 0;
                    
                    int bracket_stack = 1;
                    for(int i = 0; link[i]; ++i)
                    {
                        if(link[i] == '{')
                        {
                            ++bracket_stack;
                        }
                        else if(link[i] == '}')
                        {
                            --bracket_stack;
                        }
                        
                        if(bracket_stack == 0)
                        {
                            break;
                        }
                        
                        ++link_length;
                    }
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_code;
                    node->string = link;
                    node->string_length = link_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = link + link_length;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow code block.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A code tag expects {<code>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@Link"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    
                    char *text = 0;
                    int text_length = 0;
                    
                    char *link = 0;
                    int link_length = 0;
                    
                    text = open_bracket.string+1;
                    for(int i = 0; text[i]; ++i)
                    {
                        if(text[i] == '"')
                        {
                            text = text+i+1;
                            break;
                        }
                    }
                    for(text_length = 0; text[text_length] && text[text_length] != '"'; ++text_length);
                    
                    link = text+text_length+1;
                    for(int i = 0; link[i]; ++i)
                    {
                        if(link[i] == '"')
                        {
                            link = link+i+1;
                            break;
                        }
                    }
                    for(link_length = 0; link[link_length] && link[link_length] != '"'; ++link_length);
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_link;
                    node->string = text;
                    node->string_length = text_length;
                    node->link.url = link;
                    node->link.url_length = link_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = link + link_length+1;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow link data.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A FeatureButton tag expects {<image>,<string>,<link>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@FeatureButton"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    
                    char *image = 0;
                    int image_length = 0;
                    
                    char *text = 0;
                    int text_length = 0;
                    
                    char *link = 0;
                    int link_length = 0;
                    
                    image = open_bracket.string+1;
                    for(int i = 0; image[i]; ++i)
                    {
                        if(image[i] == '"')
                        {
                            image = image+i+1;
                            break;
                        }
                    }
                    for(image_length = 0; image[image_length] && image[image_length] != '"'; ++image_length);
                    
                    text = image+image_length+1;
                    for(int i = 0; text[i]; ++i)
                    {
                        if(text[i] == '"')
                        {
                            text = text+i+1;
                            break;
                        }
                    }
                    for(text_length = 0; text[text_length] && text[text_length] != '"'; ++text_length);
                    
                    link = text+text_length+1;
                    for(int i = 0; link[i]; ++i)
                    {
                        if(link[i] == '"')
                        {
                            link = link+i+1;
                            break;
                        }
                    }
                    for(link_length = 0; link[link_length] && link[link_length] != '"'; ++link_length);
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_feature_button;
                    node->string = text;
                    node->string_length = text_length;
                    node->feature_button.image_path = image;
                    node->feature_button.image_path_length = image_length;
                    node->feature_button.link = link;
                    node->feature_button.link_length = link_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = link + link_length+1;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow feature button data.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A FeatureButton tag expects {<image>,<string>,<link>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@Lister"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    
                    char *text = 0;
                    int text_length = 0;
                    
                    text = open_bracket.string+1;
                    for(int i = 0; text[i]; ++i)
                    {
                        if(text[i] == '"')
                        {
                            text = text+i+1;
                            break;
                        }
                    }
                    for(text_length = 0; text[text_length] && text[text_length] != '"'; ++text_length);
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_lister;
                    node->string = text;
                    node->string_length = text_length;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = text + text_length+1;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow lister data.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A FeatureButton tag expects {<image>,<string>,<link>} to follow.");
                }
            }
            
            else if(TokenMatch(tag, "@Date"))
            {
                Token open_bracket = {0};
                if(RequireToken(tokenizer, "{", &open_bracket))
                {
                    int year = 0;
                    int month = 0;
                    int day = 0;
                    
                    char *str = open_bracket.string+1;
                    
                    for(int i = 0; str[i]; ++i)
                    {
                        if(CharIsDigit(str[i]))
                        {
                            char num_str[32] = {0};
                            int j = 0;
                            for(; str[i+j] && j < sizeof(num_str) && CharIsDigit(str[j]); ++j)
                            {
                                num_str[j] = str[i+j];
                            }
                            year = CStringToInt(num_str);
                            str += i+j+1;
                            break;
                        }
                    }
                    
                    for(int i = 0; str[i]; ++i)
                    {
                        if(CharIsDigit(str[i]))
                        {
                            char num_str[32] = {0};
                            int j = 0;
                            for(; str[i+j] && j < sizeof(num_str) && CharIsDigit(str[j]); ++j)
                            {
                                num_str[j] = str[i+j];
                            }
                            month = CStringToInt(num_str);
                            str += i+j+1;
                            break;
                        }
                    }
                    
                    for(int i = 0; str[i]; ++i)
                    {
                        if(CharIsDigit(str[i]))
                        {
                            char num_str[32] = {0};
                            int j = 0;
                            for(; str[i+j] && j < sizeof(num_str) && CharIsDigit(str[j]); ++j)
                            {
                                num_str[j] = str[i+j];
                            }
                            day = CStringToInt(num_str);
                            str += i+j;
                            break;
                        }
                    }
                    
                    PageNode *node = ParseContextAllocateNode(context);
                    node->type = PAGE_NODE_TYPE_date;
                    node->string = "";
                    node->string_length = 0;
                    node->date.year = year;
                    node->date.month = month;
                    node->date.day = day;
                    node->text_style_flags = text_style_flags;
                    *node_store_target = node;
                    node_store_target = &(*node_store_target)->next;
                    
                    tokenizer->at = str;
                    if(!RequireToken(tokenizer, "}", 0))
                    {
                        PushParseError(context, tokenizer, "Expected } to follow date data.");
                    }
                }
                else
                {
                    PushParseError(context, tokenizer, "A FeatureButton tag expects {<image>,<string>,<link>} to follow.");
                }
            }
            
            else
            {
                PushParseError(context, tokenizer, "Malformed tag");
            }
            
        }
        else if(RequireTokenType(tokenizer, TOKEN_symbol, &symbol))
        {
            if(TokenMatch(symbol, "*"))
            {
                text_style_flags ^= TEXT_STYLE_ITALICS;
            }
            else if(TokenMatch(symbol, "_"))
            {
                text_style_flags ^= TEXT_STYLE_UNDERLINE;
            }
            else if(TokenMatch(symbol, "`"))
            {
                text_style_flags ^= TEXT_STYLE_MONOSPACE;
            }
            else
            {
                PushParseError(context, tokenizer, "Unexpected symbol \"%.*s\"", symbol.string_length,
                               symbol.string);
            }
        }
        else if(RequireTokenType(tokenizer, TOKEN_text, &text))
        {
            PageNode *node = ParseContextAllocateNode(context);
            node->type = PAGE_NODE_TYPE_text;
            node->string = text.string;
            node->string_length = text.string_length;
            node->text_style_flags = text_style_flags;
            *node_store_target = node;
            node_store_target = &(*node_store_target)->next;
        }
        else if(RequireTokenType(tokenizer, TOKEN_double_newline, &text))
        {
            PageNode *node = ParseContextAllocateNode(context);
            node->type = PAGE_NODE_TYPE_paragraph_break;
            node->string = 0;
            node->string_length = 0;
            node->text_style_flags = text_style_flags;
            *node_store_target = node;
            node_store_target = &(*node_store_target)->next;
        }
        
        token = PeekToken(tokenizer);
    }
    
    return result;
}

typedef struct FileProcessData
{
    int output_flags;
    char *filename_no_extension;
    char *html_output_path;
    char *md_output_path;
    char *bbcode_output_path;
    char *html_header;
    char *html_footer;
}
FileProcessData;

typedef struct ProcessedFile
{
    PageNode *root;
    char *filename;
    char *main_title;
    char *description;
    char *url;
    int date_year;
    int date_month;
    int date_day;
    int output_flags;
    char *html_header;
    char *html_footer;
    char *html_output_path;
    FILE *html_output_file;
    char *markdown_output_path;
    FILE *markdown_output_file;
    char *bbcode_output_path;
    FILE *bbcode_output_file;
}
ProcessedFile;

static void
OutputHTMLFromPageNodeTreeToFile_(PageNode *node, FILE *file, int follow_next, ProcessedFile *files, int file_count)
{
    int paragraph_active = 0;
    
    for(; node; node = node->next)
    {
        switch(node->type)
        {
            case PAGE_NODE_TYPE_title:
            {
                fprintf(file, "<h1>%.*s</h1>\n", node->string_length, node->string);
                break;
            }
            case PAGE_NODE_TYPE_sub_title:
            {
                fprintf(file, "<h2>%.*s</h2>\n", node->string_length, node->string);
                break;
            }
            case PAGE_NODE_TYPE_text:
            {
                if(!paragraph_active)
                {
                    fprintf(file, "<p>");
                    paragraph_active = 1;
                }
                
                if(node->text_style_flags & TEXT_STYLE_BOLD)
                {
                    fprintf(file, "<strong>");
                }
                if(node->text_style_flags & TEXT_STYLE_UNDERLINE)
                {
                    fprintf(file, "<u>");
                }
                if(node->text_style_flags & TEXT_STYLE_ITALICS)
                {
                    fprintf(file, "<i>");
                }
                if(node->text_style_flags & TEXT_STYLE_MONOSPACE)
                {
                    fprintf(file, "<span class=\"monospace\">");
                }
                fprintf(file, "%.*s", node->string_length, node->string);
                if(node->text_style_flags & TEXT_STYLE_MONOSPACE)
                {
                    fprintf(file, "</span>");
                }
                if(node->text_style_flags & TEXT_STYLE_ITALICS)
                {
                    fprintf(file, "</i>");
                }
                if(node->text_style_flags & TEXT_STYLE_UNDERLINE)
                {
                    fprintf(file, "</u>");
                }
                if(node->text_style_flags & TEXT_STYLE_BOLD)
                {
                    fprintf(file, "</strong>");
                }
                
                if(!node->next || (node->next->type != PAGE_NODE_TYPE_text &&
                                   node->next->type != PAGE_NODE_TYPE_link))
                {
                    fprintf(file, "</p>");
                    paragraph_active = 0;
                }
                
                break;
            }
            case PAGE_NODE_TYPE_paragraph_break:
            {
                if(paragraph_active)
                {
                    fprintf(file, "</p>");
                    paragraph_active = 0;
                }
                break;
            }
            case PAGE_NODE_TYPE_unordered_list:
            {
                fprintf(file, "<ul>\n");
                for(PageNode *list_item = node->unordered_list.first_item;
                    list_item; list_item = list_item->next)
                {
                    fprintf(file, "<li>");
                    OutputHTMLFromPageNodeTreeToFile_(list_item, file, 0, files, file_count);
                    fprintf(file, "</li>");
                }
                fprintf(file, "</ul>\n");
                break;
            }
            case PAGE_NODE_TYPE_ordered_list:
            {
                fprintf(file, "<ol>\n");
                for(PageNode *list_item = node->unordered_list.first_item;
                    list_item; list_item = list_item->next)
                {
                    fprintf(file, "<li>");
                    OutputHTMLFromPageNodeTreeToFile_(list_item, file, 0, files, file_count);
                    fprintf(file, "</li>");
                }
                fprintf(file, "</ol>\n");
                break;
            }
            case PAGE_NODE_TYPE_code:
            {
                fprintf(file, "<div class=\"code\"><pre>");
                
                enum
                {
                    CODE_TYPE_default,
                    CODE_TYPE_line_comment,
                    CODE_TYPE_block_comment,
                    CODE_TYPE_keyword,
                    CODE_TYPE_constant,
                    CODE_TYPE_tag,
                };
                
                int code_type = CODE_TYPE_default;
                
                for(int i = 0; i < node->string_length;)
                {
                    int token_length = 1;
                    
                    code_type = CODE_TYPE_default;
                    
                    if(node->string[i] == '/' && node->string[i+1] == '/')
                    {
                        code_type = CODE_TYPE_line_comment;
                        for(;
                            node->string[i+token_length] &&
                            node->string[i+token_length] != '\n';
                            ++token_length);
                        fprintf(file, "<span class=\"code_text\" style=\"color: #8cba53;\">");
                    }
                    else if(node->string[i] == '/' && node->string[i+1] == '*')
                    {
                        code_type = CODE_TYPE_block_comment;
                        for(;
                            node->string[i+token_length] &&
                            !(node->string[i+token_length] == '*' &&
                              node->string[i+token_length] == '/');
                            ++token_length);
                        fprintf(file, "<span class=\"code_text\" style=\"color: #8cba53;\">");
                    }
                    else if(CharIsAlpha(node->string[i]) || node->string[i] == '_')
                    {
                        for(;
                            node->string[i+token_length] &&
                            (node->string[i+token_length] == '_' ||
                             CharIsAlpha(node->string[i+token_length]) ||
                             CharIsDigit(node->string[i+token_length]));
                            ++token_length);
                        
                        static char *keywords[] = {
                            "auto", "break", "case", "char",
                            "const", "continue", "default", "do", "double",
                            "else", "enum", "extern", "float",
                            "for", "goto", "if", "int", "long", "register", "return",
                            "short", "signed", "sizeof", "static",
                            "struct", "switch", "typedef", "union", "unsigned",
                            "void", "volatile", "while",
                        };
                        
                        for(int k = 0; k < sizeof(keywords)/sizeof(keywords[0]); ++k)
                        {
                            if(StringMatchCaseSensitiveN(node->string+i, keywords[k], token_length) &&
                               token_length == CalculateCStringLength(keywords[k]))
                            {
                                code_type = CODE_TYPE_keyword;
                                fprintf(file, "<span class=\"code_text\" style=\"color: #f4b642;\">");
                                break;
                            }
                        }
                    }
                    else if(CharIsDigit(node->string[i]))
                    {
                        for(;
                            node->string[i+token_length] &&
                            (node->string[i+token_length] == '.' ||
                             CharIsAlpha(node->string[i+token_length]) ||
                             CharIsDigit(node->string[i+token_length]));
                            ++token_length);
                        
                        code_type = CODE_TYPE_constant;
                        fprintf(file, "<span class=\"code_text\" style=\"color: #82c4e5;\">");
                    }
                    else if(node->string[i] == '"')
                    {
                        for(;
                            node->string[i+token_length] &&
                            node->string[i+token_length] != '"';
                            ++token_length);
                        
                        ++token_length;
                        
                        code_type = CODE_TYPE_constant;
                        fprintf(file, "<span class=\"code_text\" style=\"color: #82c4e5;\">");
                    }
                    else if(node->string[i] == '\'')
                    {
                        for(;
                            node->string[i+token_length] &&
                            node->string[i+token_length] != '\'';
                            ++token_length);
                        
                        ++token_length;
                        
                        code_type = CODE_TYPE_constant;
                        fprintf(file, "<span class=\"code_text\" style=\"color: #82c4e5;\">");
                    }
                    else if(node->string[i] == '@')
                    {
                        for(;
                            node->string[i+token_length] &&
                            node->string[i+token_length] > 32;
                            ++token_length);
                        
                        ++token_length;
                        
                        code_type = CODE_TYPE_tag;
                        fprintf(file, "<span class=\"code_text\" style=\"color: #d82312;\">");
                    }
                    
                    for(int j = i; j < i + token_length; ++j)
                    {
                        if(node->string[j] == '<')
                        {
                            fprintf(file, "&lt;");
                        }
                        else if(node->string[j] == '>')
                        {
                            fprintf(file, "&gt;");
                        }
                        else if(node->string[j] == '&')
                        {
                            fprintf(file, "&amp;");
                        }
                        else
                        {
                            fprintf(file, "%c", node->string[j]);
                        }
                    }
                    
                    i += token_length;
                    
                    if(code_type != CODE_TYPE_default)
                    {
                        fprintf(file, "</span>");
                    }
                }
                
                fprintf(file, "</pre></div>");
                
                break;
            }
            case PAGE_NODE_TYPE_youtube:
            {
                fprintf(file, "<div class=\"youtube\"><iframe width=\"560\" height=\"315\" src=\"");
                
                for(int i = 0; i < node->string_length; ++i)
                {
                    int length = CalculateCStringLength("watch?v=");
                    if(StringMatchCaseSensitiveN(node->string+i, "watch?v=", length))
                    {
                        fprintf(file, "embed/");
                        i += length-1;
                    }
                    else
                    {
                        fprintf(file, "%c", node->string[i]);
                    }
                }
                
                fprintf(file, "\" frameborder=\"0\" allow=\"accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe></div>");
                break;
            }
            case PAGE_NODE_TYPE_image:
            {
                fprintf(file, "<div class=\"image_container\"><img class=\"image\" src=\"%.*s\"></div>\n", node->string_length, node->string);
                break;
            }
            case PAGE_NODE_TYPE_link:
            {
                fprintf(file, "<a class=\"link\" href=\"%.*s\">%.*s</a>",
                        node->link.url_length, node->link.url,
                        node->string_length, node->string);
                break;
            }
            case PAGE_NODE_TYPE_feature_button:
            {
                fprintf(file, "<div class=\"feature_button\">\n");
                fprintf(file, "<a href=\"%.*s\">\n",
                        node->feature_button.link_length, node->feature_button.link);
                
                fprintf(file, "<div class=\"feature_button_image\" style=\"background-image: url('%.*s');\"></div>\n",
                        node->feature_button.image_path_length, node->feature_button.image_path);
                
                fprintf(file, "<div class=\"feature_button_text\">\n");
                fprintf(file, "%.*s\n", node->string_length, node->string);
                fprintf(file, "</div>\n");
                
                fprintf(file, "</a>\n");
                fprintf(file, "</div>\n");
                break;
            }
            case PAGE_NODE_TYPE_lister:
            {
                char *prefix = node->string;
                int prefix_length = node->string_length;
                
                for(int i = 0; i < file_count; ++i)
                {
                    if(StringMatchCaseSensitiveN(files[i].filename, prefix, prefix_length))
                    {
                        char *path = files[i].html_output_path;
                        for(int j = 0; path[j]; ++j)
                        {
                            if(StringMatchCaseSensitiveN(path+j, "generated/", 10))
                            {
                                path += 10;
                                break;
                            }
                        }
                        fprintf(file, "<a class=\"lister_link\" href=\"%s\">(%i/%i/%i) %s</a>\n", path,
                                files[i].date_year, files[i].date_month, files[i].date_day, files[i].main_title);
                    }
                }
                
                break;
            }
            default: break;
        }
    }
}

static void
OutputHTMLFromPageNodeTreeToFile(ProcessedFile *page, ProcessedFile *files, int file_count)
{
    FILE *file = page->html_output_file;
    fprintf(file, "<!DOCTYPE html>\n");
    fprintf(file, "<html lang=\"en\">\n");
    fprintf(file, "<head>\n");
    fprintf(file, "<meta charset=\"utf-8\">");
    // NOTE(bvisness): Consider adding mobile-friendly styles and then adding this line
    // fprintf(file, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    fprintf(file, "<meta name=\"author\" content=\"Ryan Fleury\">\n");
    fprintf(file, "<title>%s | Ryan Fleury</title>\n", page->main_title);
    fprintf(file, "<meta property=\"og:title\" content=\"%s\">\n", page->main_title);
    fprintf(file, "<meta name=\"twitter:title\" content=\"%s\">\n", page->main_title);
    if (page->description) {
        fprintf(file, "<meta name=\"description\" content=\"%s\">\n", page->description);
        fprintf(file, "<meta property=\"og:description\" content=\"%s\">\n", page->description);
        fprintf(file, "<meta name=\"twitter:description\" content=\"View the album on Flickr.\">\n", page->description);
    }
    fprintf(file, "<link rel=\"canonical\" href=\"http://ryanfleury.net/%s\">\n", page->url);
    fprintf(file, "<meta property=\"og:type\" content=\"website\">\n");
    fprintf(file, "<meta property=\"og:url\" content=\"http://ryanfleury.net/%s\">\n", page->url);
    fprintf(file, "<meta property=\"og:site_name\" content=\"Ryan Fleury\">\n");
    fprintf(file, "<meta name=\"twitter:card\" content=\"summary\">\n");
    fprintf(file, "<meta name=\"twitter:site\" content=\"@ryanjfleury\">\n");
    fprintf(file, "<link rel=\"stylesheet\" type=\"text/css\" href=\"data/styles.css\">\n");
    fprintf(file, "</head>\n");
    fprintf(file, "<body>\n");
    if(page->html_header)
    {
        fprintf(file, "%s", page->html_header);
    }
    fprintf(file, "<div class=\"page_content\">\n");
    OutputHTMLFromPageNodeTreeToFile_(page->root, file, 1, files, file_count);
    fprintf(file, "</div>\n");
    if(page->html_footer)
    {
        fprintf(file, "%s", page->html_footer);
    }
    fprintf(file, "</body>\n");
    fprintf(file, "</html>\n");
}

static ProcessedFile
ProcessFile(char *filename, char *file, FileProcessData *process_data, ParseContext *context)
{
    ProcessedFile processed_file = {0};
    
    Tokenizer tokenizer_ = {0};
    Tokenizer *tokenizer = &tokenizer_;
    tokenizer->at = file;
    tokenizer->line = 1;
    tokenizer->file = filename;
    
    PageNode *page = ParseText(context, tokenizer);
    
    if(page)
    {
        processed_file.root = page;
        processed_file.filename = filename;
        processed_file.output_flags = process_data->output_flags;
        processed_file.html_header = process_data->html_header;
        processed_file.html_footer = process_data->html_footer;
        
        for(PageNode *node = page; node; node = node->next)
        {
            if(node->type == PAGE_NODE_TYPE_title)
            {
                processed_file.main_title = ParseContextAllocateCStringCopyN(context, node->string, node->string_length);
                break;
            }
        }

        for(PageNode *node = page; node; node = node->next)
        {
            if(node->type == PAGE_NODE_TYPE_description)
            {
                processed_file.description = ParseContextAllocateCStringCopyN(context, node->string, node->string_length);
                break;
            }
        }
        
        for(PageNode *node = page; node; node = node->next)
        {
            if(node->type == PAGE_NODE_TYPE_date)
            {
                processed_file.date_year = node->date.year;
                processed_file.date_month = node->date.month;
                processed_file.date_day = node->date.day;
                break;
            }
        }

        processed_file.url = ParseContextAllocateCStringCopy(context, process_data->filename_no_extension);
        
        if(process_data->output_flags & OUTPUT_HTML)
        {
            processed_file.html_output_path = ParseContextAllocateCStringCopy(context, process_data->html_output_path);
            processed_file.html_output_file = fopen(process_data->html_output_path, "wb");
        }
        
        if(process_data->output_flags & OUTPUT_MARKDOWN)
        {
            processed_file.markdown_output_path = ParseContextAllocateCStringCopy(context, process_data->md_output_path);
            processed_file.markdown_output_file = fopen(process_data->md_output_path, "wb");
        }
        
        if(process_data->output_flags & OUTPUT_BBCODE)
        {
            processed_file.bbcode_output_path = ParseContextAllocateCStringCopy(context, process_data->bbcode_output_path);
            processed_file.bbcode_output_file = fopen(process_data->bbcode_output_path, "wb");
        }
    }
    
    return processed_file;
}

static char *
LoadEntireFileAndNullTerminate(char *filename)
{
    char *result = 0;
    FILE *file = fopen(filename, "rb");
    if(file)
    {
        fseek(file, 0, SEEK_END);
        int file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        result = malloc(file_size+1);
        if(result)
        {
            fread(result, 1, file_size, file);
            result[file_size] = 0;
        }
    }
    return result;
}

int
main(int argument_count, char **arguments)
{
    int expected_file_count = 0;
    int output_flags = 0;
    char *html_header_path = 0;
    char *html_footer_path = 0;
    char *html_header = "";
    char *html_footer = "";
    
    for(int i = 1; i < argument_count; ++i)
    {
        
        // NOTE(rjf): Non-input arguments (just flags).
        if(StringMatchCaseInsensitive(arguments[i], "--html"))
        {
            Log("Outputting to HTML.");
            output_flags |= OUTPUT_HTML;
            arguments[i] = 0;
        }
        else if(StringMatchCaseInsensitive(arguments[i], "--markdown"))
        {
            Log("Outputting to markdown.");
            output_flags |= OUTPUT_MARKDOWN;
            arguments[i] = 0;
        }
        else if(StringMatchCaseInsensitive(arguments[i], "--bbcode"))
        {
            Log("Outputting to BBcode.");
            output_flags |= OUTPUT_BBCODE;
            arguments[i] = 0;
        }
        
        // NOTE(rjf): Arguments with input data (not just flags).
        else if(argument_count > i+1)
        {
            if(StringMatchCaseInsensitive(arguments[i], "--html_header"))
            {
                html_header_path = arguments[i+1];
                Log("HTML header path set as \"%s\".", html_header_path);
                arguments[i] = 0;
                arguments[i+1] = 0;
                ++i;
            }
            else if(StringMatchCaseInsensitive(arguments[i], "--html_footer"))
            {
                html_footer_path = arguments[i+1];
                Log("HTML footer path set as \"%s\".", html_footer_path);
                arguments[i] = 0;
                arguments[i+1] = 0;
                ++i;
            }
        }
        
        // NOTE(rjf): Just a file to parse.
        else
        {
            ++expected_file_count;
        }
        
    }
    
    if(html_header_path)
    {
        html_header = LoadEntireFileAndNullTerminate(html_header_path);
    }
    
    if(html_footer_path)
    {
        html_footer = LoadEntireFileAndNullTerminate(html_footer_path);
    }
    
    ParseContext context = {0};
    ProcessedFile files[256];
    int file_count = 0;
    
    for(int i = 1; i < argument_count; ++i)
    {
        char *filename = arguments[i];
        if(filename)
        {
            Log("Processing file \"%s\".", filename);
            
            char *file = LoadEntireFileAndNullTerminate(filename);
            
            char filename_no_extension[256] = {0};
            char html_output_path[256] = {0};
            char md_output_path[256] = {0};
            char bbcode_output_path[256] = {0};
            
            snprintf(filename_no_extension, sizeof(filename_no_extension), "%s", filename);
            char *last_period = filename_no_extension;
            for(int i = 0; filename_no_extension[i]; ++i)
            {
                if(filename_no_extension[i] == '.')
                {
                    last_period = filename_no_extension+i;
                }
            }
            *last_period = 0;
            
            snprintf(html_output_path, sizeof(html_output_path), "generated/%s.html", filename_no_extension);
            snprintf(md_output_path, sizeof(md_output_path), "generated/%s.md", filename_no_extension);
            snprintf(bbcode_output_path, sizeof(bbcode_output_path), "generated/%s.bbcode", filename_no_extension);
            
            FileProcessData process_data = {0};
            {
                process_data.output_flags = output_flags;
                process_data.filename_no_extension = filename_no_extension;
                process_data.html_output_path = html_output_path;
                process_data.md_output_path = md_output_path;
                process_data.bbcode_output_path = bbcode_output_path;
                process_data.html_header = html_header;
                process_data.html_footer = html_footer;
            }
            
            ProcessedFile processed_file = ProcessFile(filename, file, &process_data, &context);
            
            if(file_count < sizeof(files)/sizeof(files[0]))
            {
                files[file_count++] = processed_file;
            }
            else
            {
                fprintf(stderr, "ERROR: Max file count reached. @Ryan, increase this.\n");
            }
            
        }
    }
    
    // NOTE(rjf): Print errors
    if(context.error_stack_size > 0)
    {
        for(int i = 0; i < context.error_stack_size; ++i)
        {
            fprintf(stderr, "Parse Error (%s:%i): %s\n",
                    context.error_stack[i].file,
                    context.error_stack[i].line,
                    context.error_stack[i].message);
        }
    }
    
    // NOTE(rjf): Generate code for all processed files.
    {
        for(int i = 0; i < file_count; ++i)
        {
            ProcessedFile *file = files+i;
            
            if(file->html_output_file)
            {
                OutputHTMLFromPageNodeTreeToFile(file, files, file_count);
            }
            
            if(file->markdown_output_file)
            {
                // TODO(rjf)
            }
            
            if(file->bbcode_output_file)
            {
                // TODO(rjf)
            }
        }
    }
    
    return 0;
}
