using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MySqlConnector;

internal sealed record SurveyQuestionDefinition(
    string Id,
    string Type,
    string Prompt,
    IReadOnlyList<string> Options,
    bool Required);

internal sealed record CreateSurveyRequest(
    string Title,
    string Description,
    IReadOnlyList<SurveyQuestionDefinition> Questions);

internal sealed record SurveyAnswerRequest(string QuestionId, string Value);

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
        app.MapGet("/api/surveys/active", async (AuthDatabase database, CancellationToken ct) =>
        {
            await using var connection = await database.OpenConnectionAsync(ct);
            SurveyRow? survey = await LoadActiveSurveyAsync(connection, ct);
            return survey is null
                ? Results.NotFound(new { code = "NoActiveSurvey", message = "현재 진행 중인 설문이 없습니다." })
                : Results.Ok(ToPublicResponse(survey));
        }).RequireRateLimiting("survey");

        app.MapPost("/api/surveys/{surveyId}/responses", async (
            string surveyId, HttpRequest request, SubmitSurveyResponseRequest body,
            AuthDatabase database, CancellationToken ct) =>
        {
            if (!Guid.TryParse(surveyId, out _) || !Guid.TryParse(body.ResponseId, out _) ||
                body.SubmittedAtUtc == default || body.Answers is null ||
                !ValidShort(body.BuildVersion, 128) || !ValidShort(body.MapName, 128) ||
                body.ProcessRole is not ("Client" or "ListenServer" or "Standalone") ||
                (body.PlayerRole?.Trim() ?? "") is not ("" or "Mannequin" or "Survivor"))
            {
                return Results.BadRequest(new { message = "설문 응답 형식이 올바르지 않습니다." });
            }

            await using var connection = await database.OpenConnectionAsync(ct);
            SurveyRow? survey = await LoadSurveyAsync(connection, surveyId, ct);
            if (survey is null || !survey.IsActive)
                return Results.Conflict(new { message = "이 설문은 현재 응답을 받지 않습니다." });

            var questions = ParseQuestions(survey.QuestionsJson);
            if (!ValidateAnswers(questions, body.Answers, out string answerError))
                return Results.BadRequest(new { message = answerError });

            ulong? userId = await ResolveOptionalUserIdAsync(connection, request, ct);
            if (request.Headers.ContainsKey("Authorization") && userId is null)
                return Results.Unauthorized();

            string answersJson = JsonSerializer.Serialize(body.Answers.Select(answer => new
            {
                questionId = answer.QuestionId.Trim(),
                value = answer.Value.Trim()
            }));
            try
            {
                await using var command = new MySqlCommand(
                    """
                    INSERT INTO player_survey_responses
                        (response_id, survey_id, user_id, submitted_at_utc, answers_json,
                         build_version, map_name, process_role, player_role)
                    VALUES
                        (@responseId, @surveyId, @userId, @submittedAt, @answers,
                         @buildVersion, @mapName, @processRole, @playerRole);
                    """, connection) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@responseId", body.ResponseId);
                command.Parameters.AddWithValue("@surveyId", surveyId);
                command.Parameters.AddWithValue("@userId", (object?)userId ?? DBNull.Value);
                command.Parameters.AddWithValue("@submittedAt", body.SubmittedAtUtc.ToUniversalTime());
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
                    surveyId = reader.GetString(0),
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
                required = question.Required
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
                       u.account_id, u.display_name
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
                    reader.GetString(0), AsUtc(reader.GetDateTime(1)), reader.GetString(2),
                    reader.GetString(3), reader.GetString(4), reader.GetString(5),
                    reader.IsDBNull(6) ? null : reader.GetString(6),
                    reader.IsDBNull(7) ? null : reader.GetString(7),
                    reader.IsDBNull(8) ? null : reader.GetString(8)));
            }
            return Results.Ok(BuildResultsResponse(survey, questions, responses));
        }).RequireRateLimiting("admin");
    }

    private static object ToPublicResponse(SurveyRow survey) => new
    {
        surveyId = survey.Id,
        title = survey.Title,
        description = survey.Description,
        questions = JsonSerializer.Deserialize<JsonElement>(survey.QuestionsJson)
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
        var choiceStatistics = questions.Where(question => question.Type == "Choice").Select(question => new
        {
            questionId = question.Id,
            prompt = question.Prompt,
            options = question.Options.Select(option => new
            {
                option,
                count = parsed.Count(item => item.answers.Any(answer =>
                    answer.QuestionId == question.Id && answer.Value == option))
            }).ToArray()
        }).ToArray();
        var textAnswers = questions.Where(question => question.Type == "Text").Select(question => new
        {
            questionId = question.Id,
            prompt = question.Prompt,
            answers = parsed.Select(item => new
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
            answers = questions.Select(question => new
            {
                questionId = question.Id,
                prompt = question.Prompt,
                type = question.Type,
                value = item.answers.FirstOrDefault(answer => answer.QuestionId == question.Id)?.Value ?? string.Empty
            }).ToArray()
        }).ToArray();
        return new
        {
            survey = ToPublicResponse(survey),
            responseCount = responses.Count,
            choiceStatistics,
            textAnswers,
            individualResponses
        };
    }

    private static bool ValidateDefinition(CreateSurveyRequest body, out string error)
    {
        error = "설문 형식이 올바르지 않습니다.";
        if (string.IsNullOrWhiteSpace(body.Title) || body.Title.Trim().Length > 128 ||
            (body.Description?.Trim().Length ?? 0) > 512 || body.Questions is null ||
            body.Questions.Count is < 1 or > 30)
            return false;
        var ids = new HashSet<string>(StringComparer.Ordinal);
        foreach (SurveyQuestionDefinition question in body.Questions)
        {
            if (!Guid.TryParse(question.Id, out _) || !ids.Add(question.Id) ||
                question.Type is not ("Choice" or "Text") ||
                string.IsNullOrWhiteSpace(question.Prompt) || question.Prompt.Trim().Length > 256)
                return false;
            if (question.Type == "Choice")
            {
                if (question.Options is null || question.Options.Count is < 2 or > 10) return false;
                var options = question.Options.Select(option => option.Trim()).ToArray();
                if (options.Any(option => option.Length is < 1 or > 128) ||
                    options.Distinct(StringComparer.Ordinal).Count() != options.Length) return false;
            }
            else if (question.Options is { Count: > 0 }) return false;
        }
        error = string.Empty;
        return true;
    }

    private static bool ValidateAnswers(
        IReadOnlyList<SurveyQuestionDefinition> questions,
        IReadOnlyList<SurveyAnswerRequest> answers,
        out string error)
    {
        error = "설문 답변이 올바르지 않습니다.";
        if (answers.Count > questions.Count) return false;
        var answerMap = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (SurveyAnswerRequest answer in answers)
        {
            if (!answerMap.TryAdd(answer.QuestionId?.Trim() ?? string.Empty, answer.Value?.Trim() ?? string.Empty))
                return false;
        }
        foreach (SurveyQuestionDefinition question in questions)
        {
            answerMap.TryGetValue(question.Id, out string? value);
            value ??= string.Empty;
            if (question.Required && string.IsNullOrWhiteSpace(value))
            {
                error = $"필수 문항 '{question.Prompt}'에 응답해주세요.";
                return false;
            }
            if (question.Type == "Choice" && value.Length > 0 && !question.Options.Contains(value)) return false;
            if (question.Type == "Text" && value.Length > 2000) return false;
        }
        if (answerMap.Keys.Any(id => questions.All(question => question.Id != id))) return false;
        error = string.Empty;
        return true;
    }

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
        return new SurveyRow(reader.GetString(0), reader.GetString(1), reader.GetString(2),
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

    private sealed record SurveyRow(string Id, string Title, string Description, string QuestionsJson, bool IsActive);
    private sealed record SurveyResponseRow(
        string ResponseId, DateTime SubmittedAtUtc, string AnswersJson,
        string BuildVersion, string MapName, string ProcessRole, string? PlayerRole,
        string? AccountId, string? DisplayName);
}
