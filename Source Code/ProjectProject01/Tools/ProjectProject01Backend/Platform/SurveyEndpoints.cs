using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MySqlConnector;

internal sealed record SurveyQuestionDefinition(
    string Id,
    string Type,
    string Prompt,
    IReadOnlyList<string> Options,
    bool Required,
    bool AllowMultiple = false,
    int MaxSelections = 0,
    bool IsTrap = false,
    IReadOnlyList<string>? ExpectedAnswers = null);

internal sealed record CreateSurveyRequest(
    string Title,
    string Description,
    IReadOnlyList<SurveyQuestionDefinition> Questions);

internal sealed record SurveyAnswerRequest(string QuestionId, string Value, IReadOnlyList<string>? Values = null);

internal sealed record SubmitSurveyResponseRequest(
    string ResponseId,
    DateTime SubmittedAtUtc,
    IReadOnlyList<SurveyAnswerRequest> Answers,
    string BuildVersion,
    string MapName,
    string ProcessRole,
    string? PlayerRole);

internal static class SurveyEndpoints
{
    public static void MapProjectProject01Surveys(this WebApplication app)
    {
        app.MapGet("/api/surveys/active", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            if (!request.Headers.Authorization.ToString().StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            if (await ResolveOptionalUserIdAsync(connection, request, ct) is null) return Results.Unauthorized();
            SurveyRow? survey = await LoadActiveSurveyAsync(connection, ct);
            return survey is null
                ? Results.NotFound(new { code = "NoActiveSurvey", message = "현재 진행 중인 설문이 없습니다." })
                : Results.Ok(ToPublicResponse(survey));
        }).RequireRateLimiting("survey-read");

        app.MapPost("/api/surveys/{surveyId}/responses", async (
            string surveyId, HttpRequest request, SubmitSurveyResponseRequest body,
            AuthDatabase database, CancellationToken ct) =>
        {
            if (!request.Headers.Authorization.ToString().StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)) return Results.Unauthorized();
            if (!Guid.TryParse(surveyId, out _) || !Guid.TryParse(body.ResponseId, out _) ||
                body.SubmittedAtUtc == default || body.Answers is null ||
                !ValidShort(body.BuildVersion, 128) || !ValidShort(body.MapName, 128) ||
                body.ProcessRole is not ("Client" or "ListenServer" or "Standalone") ||
                (body.PlayerRole?.Trim() ?? "") is not ("" or "Mannequin" or "Survivor"))
            {
                return Results.BadRequest(new { message = "설문 응답 형식이 올바르지 않습니다." });
            }

            await using var connection = await database.OpenConnectionAsync(ct);
            ulong? userId = await ResolveOptionalUserIdAsync(connection, request, ct);
            if (userId is null) return Results.Unauthorized();
            SurveyRow? survey = await LoadSurveyAsync(connection, surveyId, ct);
            if (survey is null || !survey.IsActive)
                return Results.Conflict(new { message = "이 설문은 현재 응답을 받지 않습니다." });

            var questions = ParseQuestions(survey.QuestionsJson);
            if (!ValidateAnswers(questions, body.Answers, out string answerError))
                return Results.BadRequest(new { message = answerError });

            string[] failedTraps = questions.Where(q => q.IsTrap && !TrapMatches(q,
                body.Answers.FirstOrDefault(a => a.QuestionId == q.Id))).Select(q => q.Prompt).ToArray();

            string answersJson = JsonSerializer.Serialize(body.Answers.Select(answer => new
            {
                questionId = answer.QuestionId.Trim(),
                value = answer.Value?.Trim() ?? string.Empty,
                values = answer.Values?.Select(value => value.Trim()).ToArray()
            }));
            try
            {
                await using var command = new MySqlCommand(
                    """
                    INSERT INTO player_survey_responses
                        (response_id, survey_id, user_id, submitted_at_utc, answers_json,
                         build_version, map_name, process_role, player_role, review_status, review_reason)
                    VALUES
                        (@responseId, @surveyId, @userId, @submittedAt, @answers,
                         @buildVersion, @mapName, @processRole, @playerRole, @reviewStatus, @reviewReason);
                    """, connection) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@responseId", body.ResponseId);
                command.Parameters.AddWithValue("@reviewStatus", failedTraps.Length > 0 ? "Pending" : "Accepted");
                command.Parameters.AddWithValue("@reviewReason", string.Join(" / ", failedTraps));
                command.Parameters.AddWithValue("@surveyId", surveyId);
                command.Parameters.AddWithValue("@userId", (object?)userId ?? DBNull.Value);
                command.Parameters.AddWithValue("@submittedAt", DateTime.UtcNow);
                command.Parameters.AddWithValue("@answers", answersJson);
                command.Parameters.AddWithValue("@buildVersion", body.BuildVersion.Trim());
                command.Parameters.AddWithValue("@mapName", body.MapName.Trim());
                command.Parameters.AddWithValue("@processRole", body.ProcessRole.Trim());
                command.Parameters.AddWithValue("@playerRole", string.IsNullOrWhiteSpace(body.PlayerRole)
                    ? DBNull.Value : body.PlayerRole.Trim());
                await command.ExecuteNonQueryAsync(ct);
                return Results.Created($"/api/surveys/{surveyId}/responses/{body.ResponseId}", new
                {
                    responseId = body.ResponseId,
                    message = "설문 응답을 전송했습니다. 감사합니다."
                });
            }
            catch (MySqlException exception) when (exception.Number == 1062)
            {
                return Results.Conflict(new { message = "이 설문에는 이미 응답했습니다." });
            }
        }).RequireRateLimiting("survey");

        app.MapGet("/api/admin/surveys", async (
            HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            var surveys = new List<object>();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                """
                SELECT s.id, s.title, s.description, s.questions_json, s.is_active,
                       s.created_at_utc, s.activated_at_utc, s.closed_at_utc, COUNT(r.id)
                FROM player_surveys s
                LEFT JOIN player_survey_responses r ON r.survey_id = s.id
                GROUP BY s.id, s.title, s.description, s.questions_json, s.is_active,
                         s.created_at_utc, s.activated_at_utc, s.closed_at_utc
                ORDER BY s.created_at_utc DESC
                LIMIT 100;
                """, connection) { CommandTimeout = 5 };
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                surveys.Add(new
                {
                    surveyId = ReadIdentifier(reader, 0),
                    title = reader.GetString(1),
                    description = reader.GetString(2),
                    questions = JsonSerializer.Deserialize<JsonElement>(reader.GetString(3)),
                    isActive = reader.GetBoolean(4),
                    createdAtUtc = AsUtc(reader.GetDateTime(5)),
                    activatedAtUtc = reader.IsDBNull(6) ? (DateTime?)null : AsUtc(reader.GetDateTime(6)),
                    closedAtUtc = reader.IsDBNull(7) ? (DateTime?)null : AsUtc(reader.GetDateTime(7)),
                    responseCount = reader.GetInt32(8)
                });
            }
            return Results.Ok(new { surveys });
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/surveys", async (
            HttpRequest request, CreateSurveyRequest body, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            if (!ValidateDefinition(body, out string error))
                return Results.BadRequest(new { message = error });
            string surveyId = Guid.NewGuid().ToString("D");
            string questionsJson = JsonSerializer.Serialize(body.Questions.Select(question => new
            {
                id = question.Id.Trim(),
                type = question.Type.Trim(),
                prompt = question.Prompt.Trim(),
                options = question.Options.Select(option => option.Trim()).ToArray(),
                required = question.Required,
                allowMultiple = question.AllowMultiple,
                maxSelections = question.MaxSelections,
                isTrap = question.IsTrap,
                expectedAnswers = question.ExpectedAnswers ?? []
            }));
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                """
                INSERT INTO player_surveys (id, title, description, questions_json)
                VALUES (@id, @title, @description, @questions);
                """, connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@id", surveyId);
            command.Parameters.AddWithValue("@title", body.Title.Trim());
            command.Parameters.AddWithValue("@description", body.Description?.Trim() ?? string.Empty);
            command.Parameters.AddWithValue("@questions", questionsJson);
            await command.ExecuteNonQueryAsync(ct);
            return Results.Created($"/api/admin/surveys/{surveyId}", new
            {
                surveyId,
                message = "설문 초안을 저장했습니다. 활성화해야 클라이언트에 표시됩니다."
            });
        }).RequireRateLimiting("admin");

        app.MapPut("/api/admin/surveys/{surveyId}", async (
            string surveyId, HttpRequest request, CreateSurveyRequest body, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            if (!ValidateDefinition(body, out string error)) return Results.BadRequest(new { message = error });
            await using var connection = await database.OpenConnectionAsync(ct);
            // Keep existing responses interpretable: editing an already answered survey creates a new draft in the helper.
            await using var command = new MySqlCommand("""
                UPDATE player_surveys s SET title=@title, description=@description, questions_json=@questions
                WHERE s.id=@id AND s.is_active=FALSE
                  AND NOT EXISTS (SELECT 1 FROM player_survey_responses r WHERE r.survey_id=s.id);
                """, connection);
            command.Parameters.AddWithValue("@id", surveyId);
            command.Parameters.AddWithValue("@title", body.Title.Trim());
            command.Parameters.AddWithValue("@description", body.Description?.Trim() ?? "");
            command.Parameters.AddWithValue("@questions", JsonSerializer.Serialize(body.Questions));
            int changed = await command.ExecuteNonQueryAsync(ct);
            return changed > 0 ? Results.Ok(new { surveyId, message = "설문을 수정했습니다." })
                : Results.Conflict(new { message = "진행 중이거나 응답이 있는 설문은 새 초안으로 복사하여 수정하세요." });
        }).RequireRateLimiting("admin");

        app.MapGet("/api/admin/surveys/statistics", async (
            HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            var surveys = new List<object>();
            await using (var command = new MySqlCommand("""
                SELECT s.id,s.title,s.is_active,COUNT(r.id),
                  COALESCE(SUM(r.review_status='Accepted'),0),COALESCE(SUM(r.review_status='Pending'),0)
                FROM player_surveys s LEFT JOIN player_survey_responses r ON r.survey_id=s.id
                GROUP BY s.id,s.title,s.is_active ORDER BY s.created_at_utc DESC;
                """, connection))
            await using (var reader = await command.ExecuteReaderAsync(ct))
                while (await reader.ReadAsync(ct)) surveys.Add(new { surveyId=ReadIdentifier(reader,0),
                    title=reader.GetString(1), isActive=reader.GetBoolean(2), responseCount=reader.GetInt32(3),
                    acceptedCount=reader.GetInt32(4), pendingCount=reader.GetInt32(5) });
            var daily = new List<SurveyDailyStatistics>();
            await using (var trend = new MySqlCommand("""
                SELECT DATE(DATE_ADD(submitted_at_utc, INTERVAL 9 HOUR)),COUNT(*),
                  COALESCE(SUM(review_status='Accepted'),0),COALESCE(SUM(review_status='Pending'),0)
                FROM player_survey_responses GROUP BY 1 ORDER BY 1;
                """, connection))
            await using (var reader = await trend.ExecuteReaderAsync(ct))
                while (await reader.ReadAsync(ct)) daily.Add(new(reader.GetDateTime(0).ToString("yyyy-MM-dd"),
                    reader.GetInt32(1),reader.GetInt32(2),reader.GetInt32(3)));
            await using var totals = new MySqlCommand("""
                SELECT COUNT(*),COUNT(DISTINCT user_id),COALESCE(SUM(review_status='Accepted'),0),
                  COALESCE(SUM(review_status='Pending'),0) FROM player_survey_responses;
                """, connection);
            await using var totalReader = await totals.ExecuteReaderAsync(ct);
            await totalReader.ReadAsync(ct);
            return Results.Ok(new { surveyCount=surveys.Count, responseCount=totalReader.GetInt32(0),
                participantCount=totalReader.GetInt32(1), acceptedCount=totalReader.GetInt32(2),
                pendingCount=totalReader.GetInt32(3), surveys,
                responseTrend=FillDailyGaps(daily), trendTimeZone="Asia/Seoul" });
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/surveys/{surveyId}/responses/{responseId}/accept", async (
            string surveyId, string responseId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand("""
                UPDATE player_survey_responses SET review_status='Accepted'
                WHERE survey_id=@survey AND response_id=@response AND review_status='Pending';
                """, connection);
            command.Parameters.AddWithValue("@survey", surveyId);
            command.Parameters.AddWithValue("@response", responseId);
            return await command.ExecuteNonQueryAsync(ct) > 0 ? Results.Ok(new { message="응답을 통계에 반영했습니다." })
                : Results.NotFound();
        }).RequireRateLimiting("admin");

        app.MapDelete("/api/admin/surveys/{surveyId}/responses/{responseId}", async (
            string surveyId, string responseId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                "DELETE FROM player_survey_responses WHERE survey_id=@survey AND response_id=@response AND review_status='Pending';", connection);
            command.Parameters.AddWithValue("@survey", surveyId);
            command.Parameters.AddWithValue("@response", responseId);
            return await command.ExecuteNonQueryAsync(ct) > 0 ? Results.Ok(new { message="보류 응답을 삭제했습니다." })
                : Results.NotFound();
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/surveys/{surveyId}/activate", async (
            string surveyId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            if (!Guid.TryParse(surveyId, out _)) return Results.BadRequest(new { message = "설문 ID가 올바르지 않습니다." });
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var transaction = await connection.BeginTransactionAsync(ct);
            await using (var close = new MySqlCommand(
                "UPDATE player_surveys SET is_active = FALSE, closed_at_utc = UTC_TIMESTAMP(6) WHERE is_active = TRUE;",
                connection, transaction) { CommandTimeout = 5 })
            {
                await close.ExecuteNonQueryAsync(ct);
            }
            await using var activate = new MySqlCommand(
                """
                UPDATE player_surveys
                SET is_active = TRUE, activated_at_utc = UTC_TIMESTAMP(6), closed_at_utc = NULL
                WHERE id = @id;
                """, connection, transaction) { CommandTimeout = 5 };
            activate.Parameters.AddWithValue("@id", surveyId);
            int changed = await activate.ExecuteNonQueryAsync(ct);
            if (changed == 0)
            {
                await transaction.RollbackAsync(ct);
                return Results.NotFound(new { message = "활성화할 설문을 찾지 못했습니다." });
            }
            await transaction.CommitAsync(ct);
            return Results.Ok(new { surveyId, message = "선택한 설문을 활성화했습니다." });
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/surveys/{surveyId}/close", async (
            string surveyId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                """
                UPDATE player_surveys
                SET is_active = FALSE, closed_at_utc = UTC_TIMESTAMP(6)
                WHERE id = @id AND is_active = TRUE;
                """, connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@id", surveyId);
            int changed = await command.ExecuteNonQueryAsync(ct);
            return changed == 0
                ? Results.NotFound(new { message = "종료할 활성 설문을 찾지 못했습니다." })
                : Results.Ok(new { surveyId, message = "설문 응답 접수를 종료했습니다." });
        }).RequireRateLimiting("admin");

        app.MapDelete("/api/admin/surveys/{surveyId}", async (
            string surveyId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand(
                "DELETE FROM player_surveys WHERE id = @id AND is_active = FALSE;",
                connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@id", surveyId);
            int changed = await command.ExecuteNonQueryAsync(ct);
            return changed == 0
                ? Results.Conflict(new { message = "활성 설문은 종료한 뒤 삭제할 수 있습니다." })
                : Results.Ok(new { surveyId, message = "선택한 설문과 응답을 삭제했습니다." });
        }).RequireRateLimiting("admin");

        app.MapGet("/api/admin/surveys/{surveyId}/results", async (
            string surveyId, HttpRequest request, AuthDatabase database,
            AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!IsAdmin(request, admin)) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            SurveyRow? survey = await LoadSurveyAsync(connection, surveyId, ct);
            if (survey is null) return Results.NotFound(new { message = "설문을 찾지 못했습니다." });
            var questions = ParseQuestions(survey.QuestionsJson);
            var responses = new List<SurveyResponseRow>();
            await using var command = new MySqlCommand(
                """
                SELECT r.response_id, r.submitted_at_utc, r.answers_json,
                       r.build_version, r.map_name, r.process_role, r.player_role,
                       u.account_id, u.display_name, r.review_status, r.review_reason
                FROM player_survey_responses r
                LEFT JOIN users u ON u.id = r.user_id
                WHERE r.survey_id = @surveyId
                ORDER BY r.submitted_at_utc DESC, r.id DESC;
                """, connection) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@surveyId", surveyId);
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                responses.Add(new SurveyResponseRow(
                    ReadIdentifier(reader, 0), AsUtc(reader.GetDateTime(1)), reader.GetString(2),
                    reader.GetString(3), reader.GetString(4), reader.GetString(5),
                    reader.IsDBNull(6) ? null : reader.GetString(6),
                    reader.IsDBNull(7) ? null : reader.GetString(7),
                    reader.IsDBNull(8) ? null : reader.GetString(8), reader.GetString(9), reader.GetString(10)));
            }
            return Results.Ok(BuildResultsResponse(survey, questions, responses));
        }).RequireRateLimiting("admin");
    }

    private static object ToPublicResponse(SurveyRow survey) => new
    {
        surveyId = survey.Id,
        title = survey.Title,
        description = survey.Description,
        // Never disclose attention-check answers to the game client; always use camelCase.
        questions = ParseQuestions(survey.QuestionsJson).Select(q => new {
            id=q.Id, type=q.Type, prompt=q.Prompt, options=q.Options, required=q.Required || q.IsTrap,
            allowMultiple=q.AllowMultiple, maxSelections=SelectionLimit(q) })
    };

    private static object BuildResultsResponse(
        SurveyRow survey,
        IReadOnlyList<SurveyQuestionDefinition> questions,
        IReadOnlyList<SurveyResponseRow> responses)
    {
        var parsed = responses.Select(response => new
        {
            response,
            answers = JsonSerializer.Deserialize<List<SurveyAnswerRequest>>(response.AnswersJson,
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? []
        }).ToArray();
        var accepted = parsed.Where(item => item.response.ReviewStatus == "Accepted").ToArray();
        var choiceStatistics = questions.Where(question => question.Type is "Choice" or "OX").Select(question => new
        {
            questionId = question.Id,
            prompt = question.Prompt,
            answeredCount = accepted.Count(item => item.answers.Any(a => a.QuestionId == question.Id && SelectedValues(a).Count > 0)),
            options = question.Options.Select(option => new
            {
                option,
                count = accepted.Count(item => item.answers.Any(answer =>
                    answer.QuestionId == question.Id && SelectedValues(answer).Contains(option)))
            }).ToArray()
        }).ToArray();
        var textAnswers = questions.Where(question => question.Type == "Text").Select(question => new
        {
            questionId = question.Id,
            prompt = question.Prompt,
            answers = accepted.Select(item => new
            {
                item.response.ResponseId,
                item.response.SubmittedAtUtc,
                item.response.AccountId,
                item.response.DisplayName,
                value = item.answers.FirstOrDefault(answer => answer.QuestionId == question.Id)?.Value ?? string.Empty
            }).Where(item => !string.IsNullOrWhiteSpace(item.value)).ToArray()
        }).ToArray();
        var individualResponses = parsed.Select(item => new
        {
            item.response.ResponseId,
            item.response.SubmittedAtUtc,
            item.response.AccountId,
            item.response.DisplayName,
            item.response.BuildVersion,
            item.response.MapName,
            item.response.ProcessRole,
            item.response.PlayerRole,
            item.response.ReviewStatus,
            item.response.ReviewReason,
            answers = questions.Select(question => new
            {
                questionId = question.Id,
                prompt = question.Prompt,
                type = question.Type,
                value = FormatAnswer(item.answers.FirstOrDefault(answer => answer.QuestionId == question.Id))
            }).ToArray()
        }).ToArray();
        return new
        {
            survey = ToPublicResponse(survey),
            responseCount = responses.Count,
            acceptedCount = accepted.Length,
            pendingCount = parsed.Length - accepted.Length,
            choiceStatistics,
            textAnswers,
            individualResponses
        };
    }

    internal static bool ValidateDefinition(CreateSurveyRequest body, out string error)
    {
        error = "설문 형식이 올바르지 않습니다.";
        if (string.IsNullOrWhiteSpace(body.Title) || body.Title.Trim().Length > 128 ||
            (body.Description?.Trim().Length ?? 0) > 512 || body.Questions is null ||
            body.Questions.Count is < 1 or > 30)
            return false;
        var ids = new HashSet<string>(StringComparer.Ordinal);
        foreach (SurveyQuestionDefinition question in body.Questions)
        {
            if (question is null || question.Options is null || !Guid.TryParse(question.Id, out _) || !ids.Add(question.Id) ||
                question.Type is not ("Choice" or "Text" or "OX") ||
                string.IsNullOrWhiteSpace(question.Prompt) || question.Prompt.Trim().Length > 256)
                return false;
            if (question.Type == "Choice")
            {
                if (question.Options is null || question.Options.Count is < 2 or > 10) return false;
                if (question.Options.Any(option => option is null)) return false;
                var options = question.Options.Select(option => option.Trim()).ToArray();
                if (options.Any(option => option.Length is < 1 or > 128) ||
                    options.Distinct(StringComparer.Ordinal).Count() != options.Length) return false;
            }
            else if (question.Type == "OX")
            {
                if (question.Options is null || question.Options.Count != 2 ||
                    question.Options[0] != "O" || question.Options[1] != "X") return false;
            }
            else if (question.Options is { Count: > 0 }) return false;
            if (question.MaxSelections < 0 || question.MaxSelections > question.Options.Count ||
                (question.AllowMultiple && question.Type != "Choice")) return false;
            if (question.IsTrap && (question.ExpectedAnswers is not { Count: > 0 } ||
                question.ExpectedAnswers.Any(value => string.IsNullOrWhiteSpace(value) || value.Length > 2000) ||
                (question.Type != "Text" && (question.ExpectedAnswers.Count > SelectionLimit(question) ||
                  question.ExpectedAnswers.Any(value => !question.Options.Contains(value)))))) return false;
        }
        error = string.Empty;
        return true;
    }

    internal static bool ValidateAnswers(
        IReadOnlyList<SurveyQuestionDefinition> questions,
        IReadOnlyList<SurveyAnswerRequest> answers,
        out string error)
    {
        error = "설문 답변이 올바르지 않습니다.";
        if (answers.Count > questions.Count) return false;
        var answerMap = new Dictionary<string, SurveyAnswerRequest>(StringComparer.Ordinal);
        foreach (SurveyAnswerRequest answer in answers)
        {
            if (answer is null || answer.Values?.Any(value => value is null) == true) return false;
            if (!answerMap.TryAdd(answer.QuestionId?.Trim() ?? string.Empty, answer))
                return false;
        }
        foreach (SurveyQuestionDefinition question in questions)
        {
            answerMap.TryGetValue(question.Id, out var answer);
            string value = answer?.Value?.Trim() ?? string.Empty;
            var selected = answer is null ? [] : SelectedValues(answer);
            if ((question.Required || question.IsTrap) && (question.Type == "Text" ? value.Length == 0 : selected.Count == 0))
            {
                error = $"필수 문항 '{question.Prompt}'에 응답해주세요.";
                return false;
            }
            if (question.Type is "Choice" or "OX" && (selected.Count > SelectionLimit(question) ||
                selected.Distinct(StringComparer.Ordinal).Count() != selected.Count ||
                selected.Any(option => !question.Options.Contains(option)))) return false;
            if (question.Type == "Text" && (value.Length > 2000 || answer?.Values is { Count: > 0 })) return false;
        }
        if (answerMap.Keys.Any(id => questions.All(question => question.Id != id))) return false;
        error = string.Empty;
        return true;
    }

    internal static int SelectionLimit(SurveyQuestionDefinition q) => q.Type == "Choice" && q.AllowMultiple
        ? q.MaxSelections > 0 ? q.MaxSelections : q.Options.Count : 1;

    internal static IReadOnlyList<SurveyDailyStatistics> FillDailyGaps(IReadOnlyList<SurveyDailyStatistics> daily)
    {
        if(daily.Count==0)return [];
        var byDate=daily.GroupBy(point=>DateOnly.ParseExact(point.Date,"yyyy-MM-dd")).ToDictionary(group=>group.Key,group=>group.Last());
        var result=new List<SurveyDailyStatistics>();
        DateOnly last=byDate.Keys.Max();
        // Historical outliers must not expand millions of dates or overflow DateOnly.MaxValue.
        DateOnly first=byDate.Keys.Min();
        if(last.DayNumber-first.DayNumber>3659)first=DateOnly.FromDayNumber(last.DayNumber-3659);
        for(int day=first.DayNumber;day<=last.DayNumber;++day)
        {
            DateOnly date=DateOnly.FromDayNumber(day);
            result.Add(byDate.TryGetValue(date,out var point) ? point : new(date.ToString("yyyy-MM-dd"),0,0,0));
        }
        return result;
    }

    internal static IReadOnlyList<string> SelectedValues(SurveyAnswerRequest answer) => answer.Values is { Count: > 0 }
        ? answer.Values.Select(value => value.Trim()).ToArray()
        : string.IsNullOrWhiteSpace(answer.Value) ? [] : new[] { answer.Value.Trim() };

    internal static bool TrapMatches(SurveyQuestionDefinition question, SurveyAnswerRequest? answer) =>
        answer is not null && question.ExpectedAnswers is not null && (question.Type == "Text"
            ? question.ExpectedAnswers.Any(expected => string.Equals(expected.Trim(), answer.Value?.Trim(), StringComparison.OrdinalIgnoreCase))
            : new HashSet<string>(question.ExpectedAnswers, StringComparer.Ordinal).SetEquals(SelectedValues(answer)));

    private static string FormatAnswer(SurveyAnswerRequest? answer) => answer is null ? ""
        : answer.Values is { Count: > 0 } ? string.Join(", ", answer.Values) : answer.Value;

    private static IReadOnlyList<SurveyQuestionDefinition> ParseQuestions(string json) =>
        JsonSerializer.Deserialize<List<SurveyQuestionDefinition>>(json,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? [];

    private static async Task<SurveyRow?> LoadActiveSurveyAsync(MySqlConnection connection, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            """
            SELECT id, title, description, questions_json, is_active
            FROM player_surveys
            WHERE is_active = TRUE
            ORDER BY activated_at_utc DESC
            LIMIT 1;
            """, connection) { CommandTimeout = 5 };
        return await ReadSurveyAsync(command, ct);
    }

    private static async Task<SurveyRow?> LoadSurveyAsync(
        MySqlConnection connection, string surveyId, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            """
            SELECT id, title, description, questions_json, is_active
            FROM player_surveys WHERE id = @id LIMIT 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@id", surveyId);
        return await ReadSurveyAsync(command, ct);
    }

    private static async Task<SurveyRow?> ReadSurveyAsync(MySqlCommand command, CancellationToken ct)
    {
        await using var reader = await command.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct)) return null;
        return new SurveyRow(ReadIdentifier(reader, 0), reader.GetString(1), reader.GetString(2),
            reader.GetString(3), reader.GetBoolean(4));
    }

    private static async Task<ulong?> ResolveOptionalUserIdAsync(
        MySqlConnection connection, HttpRequest request, CancellationToken ct)
    {
        string authorization = request.Headers.Authorization.ToString();
        if (!authorization.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)) return null;
        string token = authorization[7..].Trim();
        if (token.Length == 0) return null;
        await using var command = new MySqlCommand(
            """
            SELECT user_id FROM auth_sessions
            WHERE access_token_hash = @hash AND revoked_at_utc IS NULL
              AND access_expires_at_utc > UTC_TIMESTAMP(6)
            LIMIT 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.Add("@hash", MySqlDbType.Binary, 32).Value =
            SHA256.HashData(Encoding.UTF8.GetBytes(token));
        object? value = await command.ExecuteScalarAsync(ct);
        return value is null or DBNull
            ? null
            : Convert.ToUInt64(value, System.Globalization.CultureInfo.InvariantCulture);
    }

    private static bool ValidShort(string? value, int maximum) =>
        !string.IsNullOrWhiteSpace(value) && value.Trim().Length <= maximum &&
        !value.Contains('\r') && !value.Contains('\n');

    private static bool IsAdmin(HttpRequest request, AdministratorOptions options) =>
        options.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString());

    private static DateTime AsUtc(DateTime value) => DateTime.SpecifyKind(value, DateTimeKind.Utc);

    private static string ReadIdentifier(MySqlDataReader reader, int ordinal)
    {
        object value = reader.GetValue(ordinal);
        return value is Guid guid
            ? guid.ToString("D")
            : Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? string.Empty;
    }

    private sealed record SurveyRow(string Id, string Title, string Description, string QuestionsJson, bool IsActive);
    private sealed record SurveyResponseRow(
        string ResponseId, DateTime SubmittedAtUtc, string AnswersJson,
        string BuildVersion, string MapName, string ProcessRole, string? PlayerRole,
        string? AccountId, string? DisplayName, string ReviewStatus, string ReviewReason);
}
