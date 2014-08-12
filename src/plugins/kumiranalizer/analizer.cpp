#include "analizer.h"
#include "analizer_p.h"
#include "interfaces/error.h"
#include "interfaces/lexemtype.h"
#include "lexer.h"
#include "pdautomata.h"
#include "syntaxanalizer.h"
#include "errormessages/errormessages.h"
#include "kumiranalizerplugin.h"
#include "stdlibmodules.h"

namespace KumirAnalizer {

QLocale::Language AnalizerPrivate::nativeLanguage = QLocale::Russian;

void Analizer::connectSignalImportsChanged(QObject *receiver, const char *slot)
{
    QObject::connect(d->analizer, SIGNAL(importsChanged(QStringList)),
                     receiver, slot);
}

void Analizer::setSourceLanguage(const QDir & resourcesRoot, const QLocale::Language &language)
{
    Lexer::setLanguage(resourcesRoot, language);
    AnalizerPrivate::nativeLanguage = language;
}

void Analizer::setModuleAlwaysAvailable(const QString &moduleName)
{
    if (moduleName==QString::fromLatin1("Files"))
        AnalizerPrivate::AlwaysAvailableModulesName.append(QString::fromUtf8("Файлы"));
    if (moduleName==QString::fromLatin1("Strings"))
        AnalizerPrivate::AlwaysAvailableModulesName.append(QString::fromUtf8("Строки"));
}

Analizer::Analizer(KumirAnalizerPlugin * plugin, bool teacherMode)
    : QObject(plugin)
    , teacherMode_(teacherMode)
    , plugin_(plugin)
{
    d = new AnalizerPrivate(plugin, this);
}

Shared::AnalizerInterface * Analizer::plugin()
{
    return plugin_;
}

QString Analizer::suggestFileName() const
{
    return d->analizer->suggestFileName();
}

QString Analizer::sourceText() const
{
    return d->sourceText.join("\n")+"\n";
}

std::string Analizer::rawSourceData() const
{
    const QString s = sourceText();
    QByteArray ba;
    QTextStream ts(&ba);
    ts.setGenerateByteOrderMark(true);
    ts.setCodec("UTF-8");
    ts << s;
    return std::string(ba.constData());
}

QString Analizer::createImportStatementLine(const QString &importName) const
{
    return d->lexer->importKeyword() + " " + importName;
}


void Analizer::setSourceDirName(const QString &dirName)
{
    d->analizer->setSourceDirName(dirName);
}

Shared::Analizer::LineProp Analizer::lineProp(int lineNo, const QString &text) const
{
    AST::ModulePtr currentModule = findModuleByLine(lineNo);
    QList<LexemPtr> lexems;
    d->lexer->splitIntoLexems(text, lexems, d->gatherExtraTypeNames(currentModule));
    Shared::Analizer::LineProp lp(text.length(), LxTypeEmpty);
    bool delimFound = false;
    for (int i=0; i<lexems.size(); i++) {
        LexemPtr lx = lexems[i];
        if (lx->type==LxTypeName) {
            if (algorithmsAvailabaleForModule(currentModule).contains(lx->data.trimmed())) {
                lx->type = LxNameAlg;
            }
            else if (moduleNames().contains(lx->data.trimmed())) {
                lx->type = LxNameModule;
            }
            else if (d->lexer->baseTypeByClassName(lx->data.trimmed())!=AST::TypeNone) {
                lx->type = LxNameClass;
            }
            else if (i>0) {
                if (lexems[0]->type==LxPriAlgHeader && !delimFound)
                    lx->type = LxNameAlg;
                else if (lexems[0]->type==LxPriModule && !delimFound)
                    lx->type = LxNameModule;
            }
        }
        else if (i>0 && !(lx->type & LxTypeName)) {
            delimFound = true;
        }
        for (int k=0; k<lx->length; k++) {
            int index = lx->linePos+k;
            if (index<lp.size())
                lp[index] = lx->type;
        }
    }
    return lp;
}

QStringList Analizer::algorithmsAvailabaleForModule(const AST::ModulePtr currentModule) const
{
    QStringList result;
    for (int i=0; i<d->ast->modules.size(); i++) {
        const AST::ModulePtr module = d->ast->modules[i];
        bool alwaysEnabled = i==0 || d->AlwaysAvailableModulesName.contains(module->header.name);
        bool enabled = alwaysEnabled || module->isEnabledFor(currentModule);
        if (enabled) {
            for (int j=0; j<d->ast->modules[i]->impl.algorhitms.size(); j++) {
                result << d->ast->modules[i]->impl.algorhitms[j]->header.name;
            }
        }
    }
    return result;
}

QStringList Analizer::moduleNames() const
{
    QStringList result;
    for (int i=0; i<d->ast->modules.size(); i++) {
        result << d->ast->modules[i]->header.name;
    }
    return result;
}

AnalizerPrivate::AnalizerPrivate(KumirAnalizerPlugin * plugin_,
                                 Analizer *qq)
{
    hiddenBaseLine = -1;
    q = qq;
    ast = AST::DataPtr(new AST::Data());
    lexer = new Lexer(q);
    pdAutomata = new PDAutomata(plugin_->myResourcesDir(), q);
    analizer = new SyntaxAnalizer(lexer, AlwaysAvailableModulesName, qq->teacherMode_, q);
    builtinModules.resize(16);
    ActorInterface * stdFunct = new StdLibModules::RTL;
    builtinModules[0] = stdFunct;
    createModuleFromActor_stage1(stdFunct, 0xF0);
    createModuleFromActor_stage2(stdFunct);
    ActorInterface * filesFunct = new StdLibModules::Files;
    builtinModules[1] = filesFunct;
    createModuleFromActor_stage1(filesFunct, 0xF1);
    createModuleFromActor_stage2(filesFunct);
    ActorInterface * stringsFunct = new StdLibModules::Strings;
    builtinModules[2] = stringsFunct;
    createModuleFromActor_stage1(stringsFunct, 0xF2);
    createModuleFromActor_stage2(stringsFunct);
    QList<ExtensionSystem::KPlugin*> actors = plugin_->loadedPlugins("Actor*");
    foreach (QObject *o, actors) {
        ActorInterface * actor = qobject_cast<ActorInterface*>(o);
        if (actor) {
            createModuleFromActor_stage1(actor, 0);
        }
    }
    foreach (QObject *o, actors) {
        ActorInterface * actor = qobject_cast<ActorInterface*>(o);
        if (actor) {
            createModuleFromActor_stage2(actor);
        }
    }
}

void AnalizerPrivate::setHiddenText(const QString &text, int baseLineNo)
{
//    teacherText = text;
//    hiddenBaseLine = baseLineNo;

//    // Clean old teacher algorhitms
//    foreach (Statement * st, teacherStatements) {
//        foreach (LexemPtr lx, st->data) {
//            delete lx;
//        }
//        delete st;
//    }

//    teacherStatements.clear();

//    for (int i=0; i<ast->modules.size(); i++) {

//    }

//    // Compile teacher algorhitms
//    lexer->splitIntoStatements(text.split("\n"), baseLineNo, teacherStatements, gatherExtraTypeNames());

//    // Build structure
//    pdAutomata->init(true, teacherStatements, ast);
//    pdAutomata->process();
//    pdAutomata->postProcess();

//    // Build tables for hidden algorhitms
//    analizer->init(teacherStatements, ast);
//    analizer->buildTables(false);

//    // Do complete semantic analisys
//    QList<Statement*> statementsToAnalize = statements+teacherStatements;

//    foreach (Statement * st, teacherStatements) {
//        foreach (AST::VariablePtr var, st->variables) {
//            removeAllVariables(var);
//        }
//    }

//    analizer->init(statementsToAnalize, ast);
//    analizer->processAnalisys();
//    analizer->syncStatements();

}

AnalizerPrivate::~AnalizerPrivate()
{
    delete lexer;
    delete pdAutomata;  
    delete builtinModules[0];
    delete builtinModules[1];
    delete builtinModules[2];
}

Analizer::~Analizer()
{
    delete d;
}


Shared::Analizer::TextAppend Analizer::closingBracketSuggestion(int lineNo) const
{
    for (int i=0; i<d->statements.size(); i++) {
        const TextStatementPtr st = d->statements.at(i);
        if (st->data.size()>0 && st->data.last()->lineNo==lineNo) {
            if (st->suggestedClosingBracket.first.length()>0)
                return st->suggestedClosingBracket;
        }
    }
    return QPair<QString,quint32>("",0);
}

QStringList Analizer::importModuleSuggestion(int lineNo) const
{
    for (int i=0; i<d->statements.size(); i++) {
        const TextStatementPtr st = d->statements.at(i);
        if (st->data.size()>0 && st->data.last()->lineNo==lineNo) {
            return st->suggestedImportModuleNames;
        }
    }
    return QStringList();
}

static AST::ModulePtr moduleByActor(AST::DataPtr ast, Shared::ActorInterface* actor)
{
    foreach (AST::ModulePtr mod, ast->modules) {
        if (mod->impl.actor && mod->impl.actor == actor) {
            return mod;
        }
    }
    return AST::ModulePtr();
}

void Analizer::setSourceText(const QString & text)
{
    QList<AST::ModulePtr>::iterator it = d->ast->modules.begin();
    while (it!=d->ast->modules.end()) {
        AST::ModulePtr module = *it;
        if ( module->header.type == AST::ModTypeTeacher ||
             module->header.type == AST::ModTypeTeacherMain ||
             module->header.type == AST::ModTypeUserMain ||
             module->header.type == AST::ModTypeUser ) {
            it = d->ast->modules.erase(it);
        }
        else {
            module->header.usedBy.clear();
            it++;
        }
    }
    d->statements.clear();
    d->sourceText = text.split("\n", QString::KeepEmptyParts);

    QList<TextStatementPtr> preprocessorStatements;
    d->lexer->splitIntoStatements(d->sourceText, 0, preprocessorStatements, QStringList());

    QStringList extraTypeNames;

    foreach (TextStatementPtr st, preprocessorStatements) {
        if (st->data.size()>1 && st->data.at(0)->type==LxPriImport) {
            if (st->data.at(1)->type!=LxConstLiteral && st->data.at(1)->type!=LxPriImport) {
                const QString moduleName = st->data.at(1)->data;
                foreach (const AST::ModulePtr & pmod, d->ast->modules) {
                    if (pmod->header.type==AST::ModTypeExternal &&
                            pmod->header.name==moduleName)
                    {
                        foreach (const AST::Type ptype, pmod->header.types) {
                            const QString typeName = ptype.name;
                            if (!extraTypeNames.contains(typeName))
                                extraTypeNames.append(typeName);
                        }
                        QList<Shared::ActorInterface*> deps =
                                pmod->impl.actor->usesList();
                        foreach (Shared::ActorInterface* actor, deps) {
                            AST::ModulePtr dmod = moduleByActor(d->ast, actor);
                            foreach (const AST::Type ptype, dmod->header.types) {
                                const QString typeName = ptype.name;
                                if (!extraTypeNames.contains(typeName))
                                    extraTypeNames.append(typeName);
                            }
                        }
                    }                    

                }
            }
        }
    }

    foreach (AST::ModulePtr module, d->ast->modules) {
        if (module->header.type == AST::ModTypeExternal
                && d->AlwaysAvailableModulesName.contains(module->header.name))
        {
            foreach (const AST::Type ptype, module->header.types) {
                const QString typeName = ptype.name;
                if (!extraTypeNames.contains(typeName))
                    extraTypeNames.append(typeName);
            }
        }
    }

    d->lexer->splitIntoStatements(d->sourceText, 0, d->statements, extraTypeNames);
    d->doCompilation(d->statements, AnalizerPrivate::CS_StructureAndNames);
    d->doCompilation(d->statements, AnalizerPrivate::CS_Contents);

}



QStringList AnalizerPrivate::AlwaysAvailableModulesName;

void AnalizerPrivate::createModuleFromActor_stage1(Shared::ActorInterface * actor, quint8 forcedId)
{
    // Stage 1 -- add to table and build type list
    AST::ModulePtr mod = AST::ModulePtr(new AST::Module());
    mod->builtInID = forcedId;
    mod->header.type = AST::ModTypeExternal;
    mod->header.name = actor->localizedModuleName(QLocale::Russian);
    mod->header.asciiName = actor->asciiModuleName();
    if (-1 != mod->header.name.indexOf("%")) {
        mod->header.nameTemplate = mod->header.name;
        static const QRegExp rxTemplateParameter("%[sdfb]");
        int p = 0;
        Q_FOREVER {
            p = rxTemplateParameter.indexIn(mod->header.nameTemplate, p);
            if (-1 == p) break;
            p += rxTemplateParameter.matchedLength();
            const QString cap = rxTemplateParameter.cap();
            QVariant::Type templateType;
            const QChar ch = cap[1];
            switch (ch.toLatin1()) {
            case 'd': templateType = QVariant::Int; break;
            case 'f': templateType = QVariant::Double; break;
            case 'b': templateType = QVariant::Bool; break;
            default:  templateType = QVariant::String;
            }
            mod->header.templateTypes.append(templateType);
            mod->header.templateParameters.append(QVariant::Invalid);
        }

        mod->header.name = mod->header.name.left(mod->header.name.indexOf("%")).trimmed();
    }
    if (-1 != mod->header.asciiName.indexOf("%")) {
        mod->header.asciiName = mod->header.asciiName.left(mod->header.asciiName.indexOf("%")).trimmed();
    }
    mod->impl.actor = AST::ActorPtr(actor);
    ast->modules << ModulePtr(mod);
    const Shared::ActorInterface::TypeList typeList = actor->typeList();
    for (int i=0; i<typeList.size(); i++) {
        typedef Shared::ActorInterface AI;
        AI::RecordSpecification ct = typeList[i];
        AST::Type tp;
        if (ct.localizedNames.contains(QLocale::Russian))
            tp.name = ct.localizedNames[QLocale::Russian];
        else
            tp.name = QString::fromLatin1(ct.asciiName);
        tp.actor = AST::ActorPtr(actor);
        tp.asciiName = ct.asciiName;
        AI::Record record = ct.record;
        for (int j=0; j<record.size(); j++) {
            AI::Field field = record[j];
            AI::FieldType ft = field.second;
            AST::Type afield;
            if (ft==AI::Int)
                afield.kind = AST::TypeInteger;
            else if (ft==AI::Real)
                afield.kind = AST::TypeReal;
            else if (ft==AI::Bool)
                afield.kind = AST::TypeBoolean;
            else if (ft==AI::Char)
                afield.kind = AST::TypeCharect;
            else if (ft==AI::String)
                afield.kind = AST::TypeString;
            tp.userTypeFields << AST::Field(field.first, afield);
        }
        tp.kind = AST::TypeUser;
        mod->header.types << tp;
    }    
}

static AST::Type actorTypeToASTType(const Shared::ActorInterface::FieldType ft,
                                    const Shared::ActorInterface::RecordSpecification & spec)
{
    AST::Type result;
    if (ft == Shared::ActorInterface::Void) {
        result = AST::TypeNone;
    }
    else if (ft == Shared::ActorInterface::Bool) {
        result = AST::TypeBoolean;
    }
    else if (ft == Shared::ActorInterface::Char) {
        result = AST::TypeCharect;
    }
    else if (ft == Shared::ActorInterface::Int) {
        result = AST::TypeInteger;
    }
    else if (ft == Shared::ActorInterface::Real) {
        result = AST::TypeReal;
    }
    else if (ft == Shared::ActorInterface::String) {
        result = AST::TypeString;
    }
    else if (ft == Shared::ActorInterface::RecordType) {
        result.kind = AST::TypeUser;
        result.name = spec.localizedNames.contains(QLocale::Russian)
                ? spec.localizedNames[QLocale::Russian]
                : QString::fromLatin1(spec.asciiName);
        result.asciiName = spec.asciiName;
        foreach (const Shared::ActorInterface::Field & field, spec.record) {
            AST::Field kfield;
            kfield.first = QString::fromLatin1(field.first);
            kfield.second = actorTypeToASTType(field.second, Shared::ActorInterface::RecordSpecification());
            result.userTypeFields.push_back(kfield);
        }
    }
    return result;
}

void AnalizerPrivate::createModuleFromActor_stage2(Shared::ActorInterface * actor)
{
    // Stage 2 -- build functions list
    AST::ModulePtr mod = moduleByActor(ast, actor);
    QList<Shared::ActorInterface*> deps = actor->usesList();
    foreach (Shared::ActorInterface* dep, deps) {
        AST::ModulePtr dmod = moduleByActor(ast, dep);
        dmod->header.usedBy.append(mod.toWeakRef());
    }
    foreach (const Shared::ActorInterface::Function & function, actor->functionList()) {

        static const QList<QByteArray> Operators = QList<QByteArray>()
                << "input" << "output" << "+" << "-" << "*" << "/" << "**"
                << "=" << "<>" << "<" << ">";

        AST::AlgorithmPtr alg = AST::AlgorithmPtr(new AST::Algorithm);
        alg->header.implType = AST::AlgorhitmExternal;
        alg->header.external.moduleName = actor->localizedModuleName(QLocale::Russian);
        alg->header.external.moduleAsciiName = actor->asciiModuleName();
        alg->header.external.id = function.id;
        alg->header.name = function.localizedNames.contains(QLocale::Russian)
                ? function.localizedNames[QLocale::Russian]
                : QString::fromLatin1(function.asciiName);
        alg->header.external.algorithmAsciiName = function.asciiName;
        alg->header.broken = false;
        alg->header.specialType = function.accessType == Shared::ActorInterface::TeacherModeFunction
                ? AST::AlgorithmTypeTeacher : AST::AlgorithmTypeRegular;

        alg->header.returnType = actorTypeToASTType(function.returnType, function.returnTypeSpecification);
        foreach (const Shared::ActorInterface::Argument & arg, function.arguments) {
            AST::VariablePtr karg = AST::VariablePtr(new AST::Variable);
            karg->name = arg.localizedNames.contains(QLocale::Russian)
                    ? arg.localizedNames[QLocale::Russian]
                    : QString::fromLatin1(arg.asciiName);
            karg->baseType = actorTypeToASTType(arg.type, arg.typeSpecification);
            karg->dimension = arg.dimension;
            if (arg.accessType == Shared::ActorInterface::InOutArgument) {
                karg->accessType = AST::AccessArgumentInOut;
            }
            else if (arg.accessType == Shared::ActorInterface::OutArgument) {
                karg->accessType = AST::AccessArgumentOut;
            }
            else {
                karg->accessType = AST::AccessArgumentIn;
            }
            alg->header.arguments.push_back(karg);
        }

        if (Operators.contains(function.asciiName)) {
            mod->header.operators.push_back(alg);
        }
        else {
            mod->header.algorhitms.push_back(alg);
        }
    }
}



AST::AlgorithmPtr AnalizerPrivate::findAlgorhitmByPos(AST::DataPtr data, int pos)
{
    if (pos==-1) {
        return AST::AlgorithmPtr();
    }
    foreach (const AST::ModulePtr mod, data.data()->modules) {
        foreach (const AST::AlgorithmPtr alg, mod->impl.algorhitms) {
            QList<LexemPtr> begin = alg->impl.beginLexems;
            QList<LexemPtr> end = alg->impl.endLexems;
            if (!begin.isEmpty() && !end.isEmpty()) {
                int algBegin = begin.first()->lineNo;
                int algEnd = end.first()->lineNo;
                if (algBegin!=-1 && algEnd!=-1) {
                    if (pos>algBegin && pos<algEnd) {
                        return alg;
                    }
                }
            }
        }
    }

    return AST::AlgorithmPtr();
}



QList<Shared::Analizer::Error> Analizer::errors() const
{
    QList<Shared::Analizer::Error> result;
    QList<TextStatementPtr> all = d->statements;
    for (int i=0; i<all.size(); i++) {
        foreach (const LexemPtr lx, all[i]->data) {
            if (!lx->error.isEmpty()) {
                Shared::Analizer::Error err;
                err.line = lx->lineNo;
                err.start = lx->linePos;
                err.len = lx->length;
                err.code = ErrorMessages::message(
                            "KumirAnalizer",
                            AnalizerPrivate::nativeLanguage,
                            lx->error
                            );
                if (result.size()>0 && result.last().line==err.line && result.last().code==err.code) {
                    Shared::Analizer::Error prev = result.last();
                    result.pop_back();
                    prev.len = (err.start+err.len-prev.start);
                    result << prev;
                }
                else {
                    result << err;
                }
            }
        }
    }
    return result;
}

QList<Shared::Analizer::LineProp> Analizer::lineProperties() const
{
    QList<Shared::Analizer::LineProp> result;
    QStringList lines = d->sourceText + d->teacherText.split("\n");
    for (int i=0; i<lines.size(); i++) {
        result << Shared::Analizer::LineProp(lines[i].size(), LxTypeEmpty);
    }

    result << Shared::Analizer::LineProp(0, LxTypeEmpty);
    QList<TextStatementPtr> all = d->statements;

    for (int i=0; i<all.size(); i++) {
        foreach (const LexemPtr lx, all[i]->data) {
            for (int j=lx->linePos; j<lx->linePos+lx->length; j++) {
                unsigned int value = lx->type;
                const unsigned int errorMask = LxTypeError;
                if (!lx->error.isEmpty()) {
                    value = value | errorMask;
                }
                const int lineNo = lx->lineNo;
                if (lineNo>=0 && lineNo < result.size() && j>=0 && j<result[lineNo].size())
                    result[lineNo][j] = LexemType(value);
            }
        }
    }
    return result;
}

QStringList Analizer::imports() const
{
    QStringList result;
//    for (int i=0; i<d->ast->modules.size(); i++) {
//        for (int j=0; j<d->ast->modules[i]->header.uses.size(); j++) {
//            const QString import = d->ast->modules[i]->header.uses.toList()[j];
//            if (!result.contains(import)) {
//                result << import;
//            }
//        }
//    }
    return result;
}

QList<QPoint> Analizer::lineRanks() const
{
    QList<QPoint> result;
    QStringList lines = d->sourceText + d->teacherText.split("\n");
    for (int i=0; i<lines.size(); i++) {
        result << QPoint(0,0);
    }
    QList<TextStatementPtr> all = d->statements;
    for (int i=0; i<all.size(); i++) {
        Q_ASSERT (!all[i]->data.isEmpty());
        const LexemPtr lx = all[i]->data.first();
        const int lineNo = lx->lineNo;
        const QPoint rank = all[i]->indentRank;
        if (lineNo>=0 && lineNo<result.size()) {
            if (result[lineNo]==QPoint(0,0))
                result[lineNo] = rank;
            else {
                QPoint p(result[lineNo].x(),
                         result[lineNo].y()+rank.x()+rank.y());
                result[lineNo] = p;
            }
        }
    }
    return result;
}

bool findAlgorhitmBounds( const QList<TextStatement*> & statements
                         , const AST::AlgorithmPtr alg
                         , int &beginIndex
                         , int &endIndex)
{
    LexemPtr lxFirst = alg.data()->impl.headerLexems.isEmpty()
            ? alg.data()->impl.beginLexems.first()
            : alg.data()->impl.headerLexems.first();
    LexemPtr lxLast = alg.data()->impl.endLexems.first();
    TextStatement * begin = 0;
    TextStatement * end = 0;
    foreach (TextStatement * st, statements) {
        if (st->data.first()==lxFirst) {
            begin = st;
        }
        else if (st->data.first()==lxLast) {
            end = st;
        }
        if (begin && end) {
            break;
        }
    }
    if (begin && end) {
        beginIndex = statements.indexOf(begin);
        endIndex = statements.indexOf(end);
    }
    else {
        beginIndex = endIndex = -1;
    }
    return begin && end;
}

bool AnalizerPrivate::findInstructionsBlock(
    AST::DataPtr data
    , const QList<TextStatement*> statements
    , LAS &lst
    , int &begin
    , int &end
    , AST::ModulePtr &mod
    , AST::AlgorithmPtr &alg)
{
    mod.clear();
    alg.clear();
    if (statements.isEmpty())
        return false;
    bool found = false;
    TextStatement * first = statements.first();
    TextStatement * last = statements.last();
    foreach (const AST::ModulePtr module, data.data()->modules) {
        for (int i=0; i<module->impl.initializerBody.size(); i++)
        {
            const AST::StatementPtr st = module->impl.initializerBody[i];
            if (st==first->statement) {
                mod = module;
                lst = &(module->impl.initializerBody);
                if (begin!=-999)
                    begin = i;
                found = true;
            }
            if (st==last->statement) {
                end = i+1;
            }
        }
        if (!found) {
            foreach (const AST::AlgorithmPtr algorhitm, module->impl.algorhitms) {
                for (int i=0; i<algorhitm->impl.body.size(); i++) {
                    const AST::StatementPtr st = algorhitm->impl.body[i];
                    if (st==first->statement) {
                        mod = module;
                        alg = algorhitm;
                        lst = &(algorhitm->impl.body);
                        if (begin!=-999)
                            begin = i;
                        found = true;
                    }
                    if (st==last->statement) {
                        end = i + 1;
                    }
                }
                if (begin==-999) {
                    if (last->data[0] == algorhitm->impl.endLexems[0]) {
                        found = true;
                        mod = module;
                        alg = algorhitm;
                        begin = end = algorhitm->impl.body.size();
                        lst = &(algorhitm->impl.body);
                        found = true;
                    }
                }
            }
        }
    }
    return found;
}



bool AnalizerPrivate::findInstructionsBlock(
    AST::DataPtr data
    , const QList<TextStatement *> statements
    , int pos
    , LAS &lst, int &outPos
    , AST::ModulePtr &mod
    , AST::AlgorithmPtr &alg
    )
{
    if (statements.isEmpty())
        return false;
    int searchByPos;
//    if (pos==0)
//        searchByPos = pos + 1;
    if (pos==statements.size())
        searchByPos = pos - 1;
    else
        searchByPos = pos;
    QList<TextStatement*> nearbyStatements = QList<TextStatement*>() << statements[searchByPos];
    int dummy = -999;
    return findInstructionsBlock(data, nearbyStatements, lst, dummy, outPos, mod, alg);
}

QPair<TextStatementPtr,LexemPtr> AnalizerPrivate::findSourceLexemContext(DataPtr data
                                                   , const QList<TextStatementPtr> statements
                                                   , int lineNo
                                                   , int colNo
                                                   , bool includeRightBound
                                                   )
{
    typedef QPair<TextStatementPtr,LexemPtr> ResultType;
    ResultType result = ResultType(TextStatementPtr(), LexemPtr());
    if (!data || statements.isEmpty())
        return result;

    foreach(TextStatementPtr st, statements) {
        QList<LexemPtr> lexems = st->data;
        for (int i=0; i<lexems.size(); i++) {
            LexemPtr lx = lexems[i];
            if (lineNo==lx->lineNo) {
                const QChar lastSymbol = lx->data.length() > 0
                        ? lx->data[lx->data.length()-1] : QChar();
                const int lexemStart = lx->linePos;
                const int lexemEnd = lx->linePos + lx->length +
                        (
                            includeRightBound && lastSymbol.isLetterOrNumber()
                            ? 1
                            : 0
                        )
                        ;
                if (lexemStart <= colNo && colNo < lexemEnd) {
                    result = ResultType(st, lx);
                    return result;
                }
            }
        }
    }
    return result;
}

void AnalizerPrivate::removeAllVariables(const AST::VariablePtr var)
{
    foreach (AST::ModulePtr mod, ast->modules) {
        mod->impl.globals.removeAll(var);
        foreach (AST::AlgorithmPtr alg, mod->impl.algorhitms) {
            alg->impl.locals.removeAll(var);
        }
    }
}

QList< AnalizerPrivate::ModuleStatementsBlock >
AnalizerPrivate::splitIntoModules(const QList<TextStatementPtr> &statements)
{
    // 0. Check for invalid markers
    QList<TextStatementPtr> markers;
    for (int i=0; i<statements.size(); i++) {
        TextStatementPtr st = statements.at(i);
        bool teacherMarker = st->type == Shared::LxTypeComment &&
                st->data.first()->data.trimmed().startsWith("|#%%");
        bool markedAlgHeader = false;
        // Then next block is used for compatibility to old Kumir (1.x)
        // programs
        if (markers.isEmpty() && st->type == Shared::LxPriAlgHeader) {
            LexemPtr algNameLexem;
            for (int j=0; j<st->data.size(); j++) {
                const LexemPtr lx = st->data.at(j);
                if (lx->type == LxTypeName) {
                    algNameLexem = lx;
                    break;
                }
            }
            if (algNameLexem && algNameLexem->data.startsWith('@')) {
                markedAlgHeader = true;
            }
        }
        if (teacherMarker || markedAlgHeader)
        {
            static const QString ExtraTeacherMarker = _("Extra teacher marker");
            foreach (TextStatementPtr prevMarker, markers) {
                prevMarker->setError(ExtraTeacherMarker, Lexem::BeforePDAutomata, Lexem::AsIs);
            }
            markers.push_back(st);
        }
    }
    // 1. Split by "module begin"/"module end" statements
    QList< ModuleStatementsBlock > result;
    bool teacherPart = false;
    ModuleStatementsBlock currentBlock;
    for (int i=0; i<statements.size(); i++) {
        TextStatementPtr st = statements[i];
        bool beginTeacherPart =
                !teacherPart &&
                markers.contains(st) &&
                !st->hasError();
        if (beginTeacherPart) {
            teacherPart = true;
            st->indentRank = QPoint(-1000, 0);
        }
        if (st->type == Shared::LxPriModule || beginTeacherPart) {
            if (currentBlock) {
                result << currentBlock;
                currentBlock = ModuleStatementsBlock();
                currentBlock.teacher = teacherPart;
            }
            if (st->type == Shared::LxPriModule)
                currentBlock.begin = st;
        }
        else if (st->type == Shared::LxPriEndModule) {
            currentBlock.end = st;
        }
        currentBlock.statements << st;
        if (st->type == Shared::LxPriEndModule && currentBlock.begin) {
            result << currentBlock;
            currentBlock = ModuleStatementsBlock();
            currentBlock.teacher = teacherPart;
        }
    }
    if (currentBlock)
        result << currentBlock;

    // 2. Check for extra closing "end module"
    static const QString ExtraEndModule = _("Misplaced 'end module'");
    foreach (ModuleStatementsBlock block, result) {
        foreach (TextStatementPtr st, block.statements) {
            if (st->type == Shared::LxPriEndModule && st != block.end) {
                st->setError(ExtraEndModule, Lexem::BeforePDAutomata, Lexem::AsIs);
                st->indentRank.rx() = st->indentRank.ry() = 0;
            }
        }
        if (block.end && !block.begin) {
            block.end->setError(ExtraEndModule, Lexem::BeforePDAutomata, Lexem::AsIs);
            block.end->indentRank.rx() = block.end->indentRank.ry() = 0;
        }
    }

    // 3. Check for not paired module begin
    bool previousBlockOpened = false;
    static const QString NoPairingEndModule = _("No pairing 'end module'");
    foreach (ModuleStatementsBlock block, result) {
        if (block.begin && !previousBlockOpened) {
            block.begin->indentRank = QPoint(-1000, +1);
        }
        else if (block.begin && previousBlockOpened) {
            block.begin->indentRank = QPoint(-1000, +1);
        }
        if (block.begin && !block.end) {
            block.begin->setError(NoPairingEndModule, Lexem::BeforePDAutomata, Lexem::AsIs);
            block.begin->suggestedClosingBracket.first = QString::fromUtf8("\n\n\nкон исп\n");
            block.begin->suggestedClosingBracket.second = 3;
            previousBlockOpened = true;
        }
        else {
            previousBlockOpened = false;
        }
        if (block.end && !block.end->hasError()) {
            block.end->indentRank = QPoint(-1, 0);
        }
    }

    return result;
}

void AnalizerPrivate::doCompilation(QList<TextStatementPtr> & allStatements, AnalizerPrivate::CompilationStage stage)
{   
    if (stage == CS_StructureAndNames) {
        foreach (TextStatementPtr st, allStatements) {
            foreach (LexemPtr lx, st->data) {
                if (lx->errorStage!=AST::Lexem::Lexer)
                    lx->error = "";
            }
        }

        AST::ModulePtr unnamedUserModule = AST::ModulePtr(new AST::Module);
        AST::ModulePtr unnamedTeacherModule = AST::ModulePtr(new AST::Module);
        unnamedUserModule->header.type = AST::ModTypeUserMain;
        unnamedTeacherModule->header.type = AST::ModTypeTeacherMain;

        QList<ModuleStatementsBlock> blocks = splitIntoModules(allStatements);
        for (int i=0; i<blocks.size(); i++) {
            ModuleStatementsBlock & block = blocks[i];
            QList<TextStatementPtr> & statements = block.statements;
            if (statements.startsWith(block.begin))
                statements.pop_front();
            if (statements.endsWith(block.end))
                statements.pop_back();
            AST::ModulePtr blockModule;
            if (block.begin) {
                blockModule = AST::ModulePtr(new AST::Module);
                blockModule->header.type = block.teacher?
                            AST::ModTypeTeacher : AST::ModTypeUser;
            }
            else if (block.teacher) {
                blockModule = unnamedTeacherModule;
            }
            else {
                blockModule = unnamedUserModule;
            }
            foreach (TextStatementPtr st, statements)
                st->mod = blockModule;
            if (block.begin)
                block.begin->mod = blockModule;
            if (block.end)
                block.end->mod = blockModule;
            if (blockModule != unnamedUserModule && blockModule != unnamedTeacherModule) {
                ast->modules.append(blockModule);
            }
            if (statements.size() > 0) {
                pdAutomata->init(statements, blockModule);
                pdAutomata->process();
                pdAutomata->postProcess();
            }
        }

        ast->modules.append(unnamedUserModule);
        ast->modules.append(unnamedTeacherModule);

        analizer->init(allStatements, ast);
        analizer->buildTables(false);
    }
    else {
        analizer->processAnalisys();
    }
}

const AST::DataPtr Analizer::abstractSyntaxTree() const
{
    return d->ast;
}


const AST::ModulePtr Analizer::findModuleByLine(int lineNo) const
{
    if (lineNo==-1)
        return AST::ModulePtr();

    // Find named module
    for (int i=0; i<d->ast->modules.size(); i++) {
        const AST::ModulePtr module = d->ast->modules.at(i);
        AST::ModuleType moduleType = module->header.type;
        bool useThisModule =
                moduleType == AST::ModTypeUser ||
                moduleType == AST::ModTypeUserMain;
        if (teacherMode_) {
            useThisModule = useThisModule ||
                    moduleType == AST::ModTypeTeacher ||
                    moduleType == AST::ModTypeTeacherMain;
        }
        const bool moduleHasText =
                (module->impl.beginLexems.size() > 0 &&
                module->impl.endLexems.size() > 0); // explicit begin/end

        if (useThisModule && moduleHasText) {
            const int start = module->impl.beginLexems.at(0)->lineNo;
            const int end = module->impl.endLexems.at(0)->lineNo;
            if (start <= lineNo && lineNo <= end) {
                return module;
            }
        }
    }

    // If named module not found -- return unnamed main module
    for (int i=0; i<d->ast->modules.size(); i++) {
        if (
                (d->ast->modules[i]->header.type==AST::ModTypeUserMain)
                && d->ast->modules[i]->header.name.isEmpty()
                )
            return d->ast->modules[i];
    }
    return AST::ModulePtr();
}

const AST::AlgorithmPtr Analizer::findAlgorhitmByLine(const AST::ModulePtr mod, int lineNo) const
{
    if (!mod || lineNo==-1)
        return AST::AlgorithmPtr();
    for (int i=0; i<mod.data()->impl.algorhitms.size(); i++) {
        const AST::AlgorithmPtr alg = mod.data()->impl.algorhitms.at(i);
        if (alg.data()->impl.beginLexems.size()==0 || alg.data()->impl.endLexems.size()==0)
            continue;
        const int algBegin = alg.data()->impl.beginLexems.front()->lineNo;
        const int algEnd   = alg.data()->impl.endLexems.last()->lineNo;
        if (algBegin <= lineNo && lineNo <= algEnd)
            return alg;
    }
    return AST::AlgorithmPtr();
}

QList<Shared::Analizer::Suggestion> Analizer::suggestAutoComplete(int lineNo, const QString &before, const QString &after) const
{
    const AST::ModulePtr mod = findModuleByLine(lineNo);
    const AST::AlgorithmPtr alg = findAlgorhitmByLine(mod, lineNo);
    QStringList beforeProgram;
    beforeProgram << before;
    QList<TextStatementPtr> beforeStatements;
    d->lexer->splitIntoStatements(beforeProgram, 0, beforeStatements, QStringList());
    QList<LexemPtr> afterLexems;
    d->lexer->splitIntoLexems(after, afterLexems, QStringList());
    const TextStatementPtr lastStatement = beforeStatements.size()>0? beforeStatements.last() : TextStatementPtr();
    QList<Shared::Analizer::Suggestion> result = d->analizer->suggestAutoComplete(lineNo, lastStatement, afterLexems, mod, alg);
    QList<Shared::Analizer::Suggestion> filteredResult;
    for (int i_sugg=0; i_sugg<result.size(); i_sugg++) {
        // filter by space at end
        const Shared::Analizer::Suggestion & s = result.at(i_sugg);
        if (before.endsWith(' ') && !before.trimmed().isEmpty()) {
            // suggest only if suggestion bounds by a keyword
            if (s.kind==Shared::Analizer::Suggestion::SecondaryKeyword ||
                    (lastStatement!=nullptr
                     && lastStatement->data.size()>0 &&
                        (
                        lastStatement->data.last()->type & LxTypePrimaryKwd
                        ||
                        lastStatement->data.last()->type & LxTypeSecondaryKwd
                        )
                     )
                    )
                filteredResult.push_back(s);
        }
        else {
            // regular case -- suggest it (already filtered by name)
            filteredResult.push_back(s);
        }
    }

    QList<Shared::Analizer::Suggestion> cleanResult;
        // Remove duplicates
    foreach ( const Shared::Analizer::Suggestion & s , filteredResult) {
        bool found = false;
        foreach ( const Shared::Analizer::Suggestion & ss , cleanResult ) {
            if (s.value.trimmed()==ss.value.trimmed()) {
                found = true;
                break;
            }
        }
        if (!found) {
            cleanResult.append(s);
        }
    }

    return cleanResult;
}

Shared::Analizer::ApiHelpItem Analizer::itemUnderCursor(const QString & text, int lineNo, int colNo, bool includeRightBound) const
{
    using namespace Shared::Analizer;

    // get context by line number
    QPair<TextStatementPtr,LexemPtr> context = AnalizerPrivate::findSourceLexemContext(
                d->ast, d->statements, lineNo, colNo, includeRightBound
                );    
    TextStatementPtr st = context.first;
    AST::ModulePtr contextModule = st->mod;
    AST::AlgorithmPtr contextAlgorithm = st->alg;

    // the compiled text line might be outdated, so use new one
    QList<LexemPtr> lexems;
    QStringList dummy;
    d->lexer->splitIntoLexems(text, lexems, dummy);
    LexemPtr lx;

    for (int i=0; i<lexems.size(); i++) {
        LexemPtr curLex = lexems[i];
        const QChar lastSymbol = curLex->data.length() > 0
                ? curLex->data[curLex->data.length()-1] : QChar();
        const int lexemStart = curLex->linePos;
        const int lexemEnd = curLex->linePos + curLex->length +
                (
                    includeRightBound && lastSymbol.isLetterOrNumber()
                    ? 1
                    : 0
                )
                ;
        if (lexemStart <= colNo && colNo < lexemEnd) {
            lx = curLex;
            break;
        }
    }

    ApiHelpItem result;

    if (st && lx) {
        QString name = lx->data.trimmed();

        if (LxTypePrimaryKwd & lx->type || LxTypeSecondaryKwd & lx->type) {
            result.kind = ApiHelpItem::Keyword;
            result.itemName = name;
        }
        else if (LxTypeName & lx->type) {
            // try to find algorithm
            AST::AlgorithmPtr resultAlgorithm;
            AST::ModulePtr resultModule;
            QVariantList templateParameters;
            if (d->analizer->findAlgorithm(name,
                                           contextModule, contextAlgorithm,
                                           resultModule, resultAlgorithm,
                                           templateParameters))
            {
                result.kind = ApiHelpItem::Function;
                result.itemName = resultAlgorithm->header.name;
                result.packageName = 0xF0==resultModule->builtInID
                        ? QString()
                        : resultModule->header.name
                        ;
            }
        }
    }

    return result;
}


QStringList AnalizerPrivate::gatherExtraTypeNames(const AST::ModulePtr currentModule) const
{
    QStringList result;
    if (ast) {
        for (int i=0; i<ast->modules.size(); i++) {
            const AST::ModulePtr module = ast->modules[i];
            bool alwaysEnabled = i==0 || AlwaysAvailableModulesName.contains(module->header.name);
            bool enabled = alwaysEnabled || module->isEnabledFor(currentModule);
            if (enabled) {
                for (int j=0; j<module->header.types.size(); j++) {
                    AST::Type tp = module->header.types[j];
                    result << tp.name;
                }
                if (module->impl.actor) {
                    QList<Shared::ActorInterface*> deps = module->impl.actor->usesList();
                    foreach (Shared::ActorInterface * actor, deps) {
                        const AST::ModulePtr actorMod = moduleByActor(ast, actor);
                        for (int j=0; j<actorMod->header.types.size(); j++) {
                            AST::Type tp = actorMod->header.types[j];
                            result << tp.name;
                        }
                    }
                }
            }
        }
    }
    return result;
}

} // namespace KumirAnalizer
